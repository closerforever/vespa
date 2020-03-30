// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.config.provision;

import java.util.Objects;

/**
 * The resources of a cluster
 *
 * @author bratseth
 */
public class ClusterResources {

    /** The node count in the cluster */
    private final int nodes;

    /** The number of node groups in the cluster */
    private final int groups;

    /** The resources of each node in the cluster */
    private final NodeResources nodeResources;

    public ClusterResources(int nodes, int groups, NodeResources nodeResources) {
        if (nodes > 0 && groups > 0 && nodes % groups != 0)
            throw new IllegalArgumentException("The number of nodes (" + nodes +
                                               ") must be divisible by the number of groups (" + groups + ")");
        this.nodes = nodes;
        this.groups = groups;
        this.nodeResources = Objects.requireNonNull(nodeResources);
    }

    /** Returns the total number of allocated nodes (over all groups) */
    public int nodes() { return nodes; }
    public int groups() { return groups; }
    public NodeResources nodeResources() { return nodeResources; }

    public ClusterResources with(NodeResources resources) { return new ClusterResources(nodes, groups, resources); }
    public ClusterResources withGroups(int groups) { return new ClusterResources(nodes, groups, nodeResources); }

    /** Returns true if this is smaller than the given resources in any dimension */
    public boolean smallerThan(ClusterResources other) {
        if (this.nodes < other.nodes) return true;
        if (this.groups < other.groups) return true;
        if ( ! this.nodeResources.justNumbers().satisfies(other.nodeResources.justNumbers())) return true;
        return false;
    }

    /** Returns true if this is within the given limits (inclusive) */
    public boolean isWithin(ClusterResources min, ClusterResources max) {
        if (this.smallerThan(min)) return false;
        if (max.smallerThan(this)) return false;
        return true;
    }

    @Override
    public boolean equals(Object o) {
        if (o == this) return true;
        if ( ! (o instanceof ClusterResources)) return false;

        ClusterResources other = (ClusterResources)o;
        if (other.nodes != this.nodes) return false;
        if (other.groups != this.groups) return false;
        if ( ! other.nodeResources.equals(this.nodeResources)) return false;
        return true;
    }

    @Override
    public int hashCode() {
        return Objects.hash(nodes, groups, nodeResources);
    }

    @Override
    public String toString() {
        return nodes + " nodes" +
               (groups > 1 ? " (in " + groups + " groups)" : "") +
               " with " + nodeResources;
    }

}
