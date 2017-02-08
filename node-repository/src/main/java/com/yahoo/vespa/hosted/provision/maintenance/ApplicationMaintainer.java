// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.provision.maintenance;

import com.yahoo.config.provision.ApplicationId;
import com.yahoo.config.provision.Deployer;
import com.yahoo.config.provision.Deployment;
import com.yahoo.transaction.Mutex;
import com.yahoo.vespa.hosted.provision.Node;
import com.yahoo.vespa.hosted.provision.NodeRepository;

import java.time.Duration;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import java.util.logging.Level;
import java.util.stream.Collectors;

/**
 * The application maintainer regularly redeploys all applications.
 * This is necessary because applications may gain and lose active nodes due to nodes being moved to and from the
 * failed state. This is corrected by redeploying the applications periodically.
 * It can not (at this point) be done reliably synchronously as part of the fail/reactivate call due to the need for this
 * to happen at a node having the deployer.
 *
 * @author bratseth
 */
public class ApplicationMaintainer extends Maintainer {

    private final Deployer deployer;
    private final Function<NodeRepository, Set<ApplicationId>> activeApplicationsGetter;

    public ApplicationMaintainer(Deployer deployer, NodeRepository nodeRepository, Duration rate) {
        this(deployer, nodeRepository, rate, ApplicationMaintainer::getActiveApplications);
    }

    ApplicationMaintainer(Deployer deployer, NodeRepository nodeRepository, Duration rate,
                          Function<NodeRepository, Set<ApplicationId>> activeApplicationsGetter) {
        super(nodeRepository, rate);
        this.deployer = deployer;
        this.activeApplicationsGetter = activeApplicationsGetter;
    }

    @Override
    protected void maintain() {
        Set<ApplicationId> applications = activeApplicationsGetter.apply(nodeRepository());

        for (ApplicationId application : applications) {
            try {
                // An application might change it's state between the time the set of applications is retrieved and the
                // time deployment happens. Lock on application and check if it's still active
                try (Mutex lock = nodeRepository().lock(application)) {
                    if (isApplicationActive(application)) {
                        Optional<Deployment> deployment = deployer.deployFromLocalActive(application, Duration.ofMinutes(30));
                        if ( ! deployment.isPresent()) continue; // this will be done at another config server

                        deployment.get().prepare();
                        deployment.get().activate();
                    }
                }
            }
            catch (RuntimeException e) {
                log.log(Level.WARNING, "Exception on maintenance redeploy of " + application, e);
            }
        }
    }

    @Override
    public String toString() { return "Periodic application redeployer"; }

    private boolean isApplicationActive(ApplicationId application) {
        return !nodeRepository().getNodes(application, Node.State.active).isEmpty();
    }

    static Set<ApplicationId> getActiveApplications(NodeRepository nodeRepository) {
        return nodeRepository.getNodes(Node.State.active).stream()
                .map(node -> node.allocation().get().owner())
                .collect(Collectors.toSet());
    }

}
