#pragma once
#include "instance_services.hpp"
#include <QtWidgets>
namespace neko::ui {
void showHealthReport(QWidget *parent, const Json &instance, const QList<InstanceCheck> &report);
}
