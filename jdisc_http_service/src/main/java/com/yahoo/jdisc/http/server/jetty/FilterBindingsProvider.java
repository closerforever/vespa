// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.jdisc.http.server.jetty;

import com.yahoo.component.ComponentId;
import com.yahoo.component.provider.ComponentRegistry;
import com.yahoo.container.di.componentgraph.Provider;
import com.yahoo.jdisc.http.server.jetty.FilterChainRepository;
import com.yahoo.jdisc.application.BindingRepository;
import com.yahoo.jdisc.http.ServerConfig;
import com.yahoo.jdisc.http.filter.RequestFilter;
import com.yahoo.jdisc.http.filter.ResponseFilter;
import com.yahoo.jdisc.http.filter.SecurityRequestFilter;
import com.yahoo.jdisc.http.server.jetty.FilterBindings;

import java.util.ArrayList;
import java.util.List;

/**
 * Provides filter bindings based on vespa config.
 *
 * @author bakksjo
 */
public class FilterBindingsProvider implements Provider<FilterBindings> {

    final BindingRepository<RequestFilter> requestFilters = new BindingRepository<>();
    final BindingRepository<ResponseFilter> responseFilters = new BindingRepository<>();

    public FilterBindingsProvider(ComponentId componentId,
                                  ServerConfig config,
                                  FilterChainRepository filterChainRepository,
                                  ComponentRegistry<SecurityRequestFilter> legacyRequestFilters) {
        ComponentId serverId = componentId.getNamespace();
        try {
            FilterUtil.setupFilters(
                    componentId,
                    legacyRequestFilters,
                    toFilterSpecs(config.filter()),
                    filterChainRepository,
                    requestFilters,
                    responseFilters);
        } catch (Exception e) {
            throw new RuntimeException("Invalid config for http server " + serverId, e);
        }
    }

    private List<FilterUtil.FilterSpec> toFilterSpecs(List<ServerConfig.Filter> inFilters) {
        List<FilterUtil.FilterSpec> outFilters = new ArrayList<>();
        for (ServerConfig.Filter inFilter : inFilters) {
            outFilters.add(new FilterUtil.FilterSpec(inFilter.id(), inFilter.binding()));
        }
        return outFilters;
    }

    @Override
    public FilterBindings get() {
        return new FilterBindings(requestFilters, responseFilters);
    }

    @Override
    public void deconstruct() {}

}
