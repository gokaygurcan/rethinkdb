// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef CLUSTERING_ADMINISTRATION_SERVERS_NETWORK_LOGGER_HPP_
#define CLUSTERING_ADMINISTRATION_SERVERS_NETWORK_LOGGER_HPP_

#include <map>
#include <set>
#include <string>

#include "clustering/administration/servers/server_metadata.hpp"
#include "clustering/administration/metadata.hpp"
#include "rpc/semilattice/view.hpp"

/* This class is responsible for writing log messages when a peer connects or
disconnects from us */

class network_logger_t {
public:
    network_logger_t(
        peer_id_t us,
        watchable_map_t<peer_id_t, cluster_directory_metadata_t> *directory_view);

    /* This contains an entry for a given server ID if we can see a server with that ID.
    This will be piped over the network to other servers to form the full connections
    map. */
    watchable_map_t<server_id_t, empty_value_t> *get_local_connections_map() {
        return &connected_servers;
    }

private:
    void on_change();
    std::string pretty_print_server(server_id_t id);

    peer_id_t us;
    watchable_map_t<peer_id_t, cluster_directory_metadata_t> *directory_view;

    watchable_map_t<peer_id_t, cluster_directory_metadata_t>::all_subs_t directory_subscription;
    semilattice_read_view_t<servers_semilattice_metadata_t>::subscription_t semilattice_subscription;

    /* Whenever the directory changes, we compare the directory to
    `connected_servers` and `connected_proxies` to see what servers have
    connected or disconnected. */
    watchable_map_var_t<server_id_t, empty_value_t> connected_servers;
    std::set<peer_id_t> connected_proxies;
};

#endif /* CLUSTERING_ADMINISTRATION_SERVERS_NETWORK_LOGGER_HPP_ */
