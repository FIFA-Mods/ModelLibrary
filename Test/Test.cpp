#include "Model.h"

int main() {
    Model m;
    m.ReadFbx("head_163587_0_0_mesh.fbx");
    m.WriteFbx("head_163587_0_0_mesh_rewritten.fbx");
    m.WriteObj("head_163587_0_0_mesh.obj");
}
