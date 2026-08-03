#include "Model.h"
#include "Error.h"
#include "Utils.h"

using namespace std;
using namespace std::filesystem;

int main() {
    auto dir = current_path();
    for (auto const &i : directory_iterator(dir)) {
        if (is_regular_file(i.path()) && i.path().extension() == ".fbx") {
            Model m(i.path());
            path jsonPath = i.path();
            jsonPath.replace_extension(".json");
            m.ToJson(jsonPath);
        }
    }
}
