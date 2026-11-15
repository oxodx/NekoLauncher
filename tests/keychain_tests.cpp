#include <QtCore>
#include <iostream>
#include <qt6keychain/keychain.h>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto service = "NekoLauncher-Test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto run = [](QKeychain::Job &job) {
        job.setAutoDelete(false);
        job.setInsecureFallback(false);
        job.setKey("roundtrip");
        QEventLoop loop;
        QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();
        if (job.error())
            throw std::runtime_error(job.errorString().toStdString());
    };
    try {
        QKeychain::WritePasswordJob write(service);
        write.setBinaryData("synthetic-test-credential");
        run(write);
        auto cleanup = qScopeGuard([&] {
            QKeychain::DeletePasswordJob remove(service);
            try {
                run(remove);
            } catch (...) {
            }
        });
        QKeychain::ReadPasswordJob read(service);
        run(read);
        if (read.binaryData() != "synthetic-test-credential")
            throw std::runtime_error("Credential mismatch");
        QKeychain::DeletePasswordJob remove(service);
        run(remove);
        cleanup.dismiss();
        std::cout << "Secure credential write/read/delete passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
