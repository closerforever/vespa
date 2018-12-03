// Copyright 2018 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.security.tls;

import com.yahoo.security.KeyUtils;
import com.yahoo.security.X509CertificateBuilder;
import com.yahoo.security.tls.authz.PeerAuthorizerTrustManager.Mode;
import com.yahoo.security.tls.policy.AuthorizedPeers;
import com.yahoo.security.tls.policy.HostGlobPattern;
import com.yahoo.security.tls.policy.PeerPolicy;
import com.yahoo.security.tls.policy.RequiredPeerCredential;
import com.yahoo.security.tls.policy.Role;
import org.junit.Test;

import javax.net.ssl.SSLEngine;
import javax.security.auth.x500.X500Principal;
import java.security.KeyPair;
import java.security.cert.X509Certificate;
import java.time.Instant;

import static com.yahoo.security.KeyAlgorithm.RSA;
import static com.yahoo.security.SignatureAlgorithm.SHA256_WITH_RSA;
import static com.yahoo.security.X509CertificateBuilder.generateRandomSerialNumber;
import static java.time.Instant.EPOCH;
import static java.time.temporal.ChronoUnit.DAYS;
import static java.util.Collections.singleton;
import static java.util.Collections.singletonList;
import static org.assertj.core.api.Assertions.assertThat;

/**
 * @author bjorncs
 */
public class DefaultTlsContextTest {

    @Test
    public void can_create_sslcontext_from_credentials() {
        KeyPair keyPair = KeyUtils.generateKeypair(RSA);

        X509Certificate certificate = X509CertificateBuilder
                .fromKeypair(keyPair, new X500Principal("CN=dummy"), EPOCH, Instant.now().plus(1, DAYS), SHA256_WITH_RSA, generateRandomSerialNumber())
                .build();

        AuthorizedPeers authorizedPeers = new AuthorizedPeers(
                singleton(
                        new PeerPolicy(
                                "dummy-policy",
                                singleton(new Role("dummy-role")),
                                singletonList(new RequiredPeerCredential(RequiredPeerCredential.Field.CN, new HostGlobPattern("dummy"))))));

        DefaultTlsContext tlsContext =
                new DefaultTlsContext(singletonList(certificate), keyPair.getPrivate(), singletonList(certificate), authorizedPeers, Mode.ENFORCE);

        SSLEngine sslEngine = tlsContext.createSslEngine();
        assertThat(sslEngine).isNotNull();
        String[] enabledCiphers = sslEngine.getEnabledCipherSuites();
        assertThat(enabledCiphers).isNotEmpty();
        assertThat(enabledCiphers).isSubsetOf(DefaultTlsContext.ALLOWED_CIPHER_SUITS.toArray(new String[0]));
    }

}