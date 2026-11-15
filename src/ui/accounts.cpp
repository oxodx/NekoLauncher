#include "ui/widgets.hpp"
#include "window.hpp"
#include <qt6keychain/keychain.h>
using namespace neko;
using namespace neko::ui;

QWidget *Window::accountsPage() {
    QVBoxLayout *l;
    auto *w = page(l, "Your account",
                   "Sign in with Microsoft for online play, or use a local profile when playing offline.");
    auto *card = new QGroupBox("Microsoft account");
    auto *c = new QVBoxLayout(card);
    c->setSpacing(18);
    auto *profile = new QHBoxLayout;
    profile->setSpacing(16);
    auto *avatar = label("", "profileAvatar");
    avatar->setFixedSize(64, 64);
    avatar->setAlignment(Qt::AlignCenter);
    profile->addWidget(avatar);
    auto *profileName = new QVBoxLayout;
    identity->setObjectName("accountName");
    identity->setText("No account connected");
    identity->show();
    profileName->addWidget(identity);
    profileName->addWidget(label("Minecraft: Java Edition", "muted"));
    profile->addLayout(profileName, 1);
    c->addLayout(profile);
    c->addWidget(label("Sign in in your browser using a device code. Neko never asks for your password. "
                       "Your account is saved in your system credential store and refreshed automatically.",
                       "muted"));
    profileActions = new QBoxLayout(QBoxLayout::LeftToRight);
    c->addLayout(profileActions);
    button("Sign in with Microsoft", profileActions, [this] { signIn(); })->setObjectName("primary");
    button("Sign out", profileActions, [this] {
        if (busy) {
            log("Wait for the current operation before signing out.");
            return;
        }
        ++accountGeneration;
        auto *job = new QKeychain::DeletePasswordJob("NekoLauncher", this);
        job->setKey("microsoft-account");
        job->setInsecureFallback(false);
        connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job *result) {
            if (result->error() && result->error() != QKeychain::EntryNotFound)
                log("Could not delete saved account: " + result->errorString());
        });
        job->start();
        account = {};
        identity->setText("Signed out");
        accountButton->setText("Local profile");
        offline->setChecked(true);
        log("Signed out. Saved credential removal requested.");
    });
    l->addWidget(card);
    auto *local = new QGroupBox("Local profile");
    auto *f = new QFormLayout(local);
    offlineName = entry(settings.value("offlineName", "Player").toString());
    offlineName->setMaxLength(16);
    offlineName->setValidator(
        new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9_]{1,16}"), offlineName));
    f->addRow("Player name", offlineName);
    offline = new QCheckBox("Use offline mode");
    offline->setChecked(true);
    f->addRow(offline);
    connect(offlineName, &QLineEdit::textChanged, this,
            [this](const QString &s) { settings.setValue("offlineName", s); });
    l->addWidget(local);
    l->addWidget(label("Offline mode works with downloaded game files and local worlds. Authenticated "
                       "multiplayer needs a valid Microsoft account with Minecraft: Java Edition.",
                       "muted"));
    l->addStretch();
    return w;
}
void Window::signIn() {
    if (busy)
        return;
    ++accountGeneration;
    auto id = settings.value("clientId").toString().trimmed();
    if (id.isEmpty())
        id = QString::fromLatin1(DefaultMicrosoftClientId);
    auto result = std::make_shared<Account>();
    task(
        [this, id, result](Engine &e) {
            *result = e.authenticate(id, [this](QString url, QString code) {
                QMetaObject::invokeMethod(
                    this,
                    [this, url, code] {
                        QMessageBox box(this);
                        box.setWindowTitle("Microsoft sign-in");
                        box.setText("Visit " + url + "\n\nEnter code: " + code +
                                    "\n\nThis code expires shortly. Complete sign-in in your browser.");
                        box.setTextInteractionFlags(Qt::TextSelectableByMouse);
                        auto *open = box.addButton("Open browser", QMessageBox::ActionRole);
                        box.addButton(QMessageBox::Close);
                        connect(open, &QPushButton::clicked, this,
                                [url] { QDesktopServices::openUrl(QUrl(url)); });
                        box.exec();
                    },
                    Qt::QueuedConnection);
            });
            return QVariant{};
        },
        [this, result](QVariant) {
            account = *result;
            saveAccount();
            identity->setText(account.name + " · Microsoft");
            accountButton->setText(account.name);
            offline->setChecked(false);
            log("Signed in as " + account.name);
        });
}
void Window::saveAccount() {
    auto *job = new QKeychain::WritePasswordJob("NekoLauncher", this);
    job->setKey("microsoft-account");
    job->setInsecureFallback(false);
    job->setBinaryData(QJsonDocument(Json{{"name", account.name},
                                          {"uuid", account.uuid},
                                          {"token", account.token},
                                          {"expires", double(account.expires)},
                                          {"refreshToken", account.refreshToken},
                                          {"clientId", account.clientId}})
                           .toJson(QJsonDocument::Compact));
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job *result) {
        if (result->error())
            log("Account works for this session, but could not be saved securely: " + result->errorString());
        else
            log("Account saved in the system credential store.");
    });
    job->start();
}
void Window::restoreAccount() {
    const auto generation = accountGeneration;
    auto *job = new QKeychain::ReadPasswordJob("NekoLauncher", this);
    job->setKey("microsoft-account");
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job, generation](QKeychain::Job *result) {
        if (result->error()) {
            if (result->error() != QKeychain::EntryNotFound)
                log("Could not open saved account: " + result->errorString());
            return;
        }
        if (generation != accountGeneration || !account.uuid.isEmpty())
            return;
        auto j = QJsonDocument::fromJson(job->binaryData()).object();
        account = {j["name"].toString(),         j["uuid"].toString(),
                   j["token"].toString(),        qint64(j["expires"].toDouble()),
                   j["refreshToken"].toString(), j["clientId"].toString()};
        if (account.uuid.isEmpty()) {
            account = {};
            return;
        }
        identity->setText(account.name + " · Microsoft");
        accountButton->setText(account.name);
        offline->setChecked(false);
        log("Restored Microsoft account " + account.name +
            ". Session refreshes automatically when launching.");
    });
    job->start();
}
