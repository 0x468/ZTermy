#include "domain/ssh/SshIdentityResolver.h"

#include <QTest>

class SshIdentityResolverTests final : public QObject
{
    Q_OBJECT

private slots:
    void preservesLegacyProfileAuthentication();
    void resolvesReusableKeyAndCertificateIdentities();
    void reportsMissingReferences();
};

void SshIdentityResolverTests::preservesLegacyProfileAuthentication()
{
    const ztermy::ssh::SshProfile profile{.id = "profile",
                                          .name = "Host",
                                          .host = "host",
                                          .username = "root",
                                          .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
                                          .credentialReference = "profile"};
    const auto resolved = ztermy::ssh::resolveSshIdentity(profile, {});
    QVERIFY(resolved);
    QCOMPARE(resolved->username, std::string("root"));
    QCOMPARE(resolved->authentication, ztermy::ssh::SshAuthenticationMethod::Password);
    QVERIFY(resolved->credentialRequired);
}

void SshIdentityResolverTests::resolvesReusableKeyAndCertificateIdentities()
{
    const ztermy::ssh::SshKeychainCatalog catalog{
        .keys = {{.id = "certificate",
                  .label = "Certificate",
                  .kind = ztermy::ssh::SshKeyKind::Certificate,
                  .source = ztermy::ssh::SshKeySource::Reference,
                  .privateKeyPath = "id_ed25519",
                  .certificatePath = "id_ed25519-cert.pub"}},
        .identities = {{.id = "identity",
                        .label = "Production",
                        .username = "deploy",
                        .authentication = ztermy::ssh::SshIdentityAuthentication::Certificate,
                        .keyId = "certificate",
                        .credentialRequired = false}},
    };
    const ztermy::ssh::SshProfile profile{.id = "profile",
                                          .name = "Host",
                                          .host = "host",
                                          .username = "legacy",
                                          .identityReference = "identity",
                                          .authentication = ztermy::ssh::SshAuthenticationMethod::Password};
    const auto resolved = ztermy::ssh::resolveSshIdentity(profile, catalog);
    QVERIFY(resolved);
    QCOMPARE(resolved->username, std::string("deploy"));
    QCOMPARE(resolved->authentication, ztermy::ssh::SshAuthenticationMethod::PrivateKey);
    QCOMPARE(resolved->privateKeyPath, std::string("id_ed25519"));
    QCOMPARE(resolved->publicKeyPath, std::string("id_ed25519-cert.pub"));
}

void SshIdentityResolverTests::reportsMissingReferences()
{
    const ztermy::ssh::SshProfile profile{.id = "profile",
                                          .name = "Host",
                                          .host = "host",
                                          .username = "root",
                                          .identityReference = "missing",
                                          .authentication = ztermy::ssh::SshAuthenticationMethod::Password};
    const auto resolved = ztermy::ssh::resolveSshIdentity(profile, {});
    QVERIFY(!resolved);
    QCOMPARE(resolved.error(), ztermy::ssh::SshIdentityResolutionError::MissingIdentity);
}

QTEST_GUILESS_MAIN(SshIdentityResolverTests)

#include "ssh_identity_resolver_tests.moc"
