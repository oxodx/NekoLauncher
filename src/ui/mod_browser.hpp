#pragma once
#include "core.hpp"
#include <QWidget>
namespace neko::ui {
bool confirmModPlan(QWidget *parent, const Json &instance, const Json &plan);
}
