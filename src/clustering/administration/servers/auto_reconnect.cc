// Copyright 2010-2012 RethinkDB, all rights reserved.
#include "clustering/administration/servers/auto_reconnect.hpp"

#include "errors.hpp"
#include <boost/bind.hpp>

#include "arch/timing.hpp"
#include "concurrency/wait_any.hpp"

auto_reconnector_t::auto_reconnector_t(
        connectivity_cluster_t *connectivity_cluster_,
        connectivity_cluster_t::run_t *connectivity_cluster_run_,
        server_config_client_t *server_config_client_) :
    connectivity_cluster(connectivity_cluster_),
    connectivity_cluster_run(connectivity_cluster_run_),
    server_config_client(server_config_client_),
    server_id_subs(
        server_config_client->get_peer_to_server_map(),
        std::bind(&auto_reconnecttor_t::on_connect_or_disconnect, this, ph::_1),
        false),
    connection_subs(
        connectivity_cluster->get_connections(),
        std::bind(&auto_reconnector_t::on_connect_or_disconnect, this, ph::_1),
        true)
    { }

void auto_reconnector_t::on_connect_or_disconnect(const peer_id_t &peer_id) {
    boost::optional<server_id_t> server_id =
        server_config_client->get_peer_to_server_map()->get_key(peer_id);
    boost::optional<connectivity_cluster_t::connection_pair_t> conn =
        connectivity_cluster->get_connections()->get_key(peer_id);
    if (static_cast<bool>(server_id) && static_cast<bool>(conn)) {
        addresses[*server_id] = conn->first->get_peer_address();
        server_ids[peer_id] = *server_id;
    } else if (!static_cast<bool>(server_id) && !static_cast<bool>(conn)) {
        auto it = server_ids.find(peer_id);
        if (it != server_ids.end()) {
            coro_t::spawn_sometime(std::bind(&auto_reconnector_t::try_reconnect, this,
                it->second, drainer.lock()));
            server_ids.erase(it);
        }
    }
}

static const int initial_backoff_ms = 50;
static const int max_backoff_ms = 1000 * 15;
static const double backoff_growth_rate = 1.5;

void auto_reconnector_t::try_reconnect(const server_id_t &server,
                                       auto_drainer_t::lock_t keepalive) {
    peer_address_t last_known_address;
    auto it = addresses.find(server);
    guarantee(it != addresses.end());
    last_known_address = it->second;

    cond_t reconnected;
    watchable_map_t<server_id_t, peer_id_t>::key_subs_t subs(
        server_config_client->get_server_to_peer_map(),
        server,
        [&](const peer_id_t *pid) {
            if (pid != nullptr) {
                reconnected.pulse_if_not_already_pulsed();
            }
        },
        true);

    wait_any_t interruptor(&reconnected, keepalive.get_drain_signal());

    int backoff_ms = initial_backoff_ms;
    try {
        while (true) {
            connectivity_cluster_run->join(last_known_address);
            signal_timer_t timer;
            timer.start(backoff_ms);
            wait_interruptible(&timer, &interruptor);
            guarantee(backoff_ms * backoff_growth_rate > backoff_ms, "rounding screwed it up");
            backoff_ms = std::min(static_cast<int>(backoff_ms * backoff_growth_rate), max_backoff_ms);
        }
    } catch (const interrupted_exc_t &) {
        /* ignore; this is how we escape the loop */
    }
}

