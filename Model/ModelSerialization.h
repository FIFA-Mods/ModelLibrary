#pragma once
#include "ModelClass.h"

void WriteModelJson(Model const &model, std::filesystem::path const &filename, int indent = 2);
Model ReadModelJson(std::filesystem::path const &filename);
