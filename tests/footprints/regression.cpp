// From the source root: g++ -std=c++14 -Wall -Wextra -Werror -I. tests/footprints/regression.cpp -o footprints-test
#include "apps/openmw/mwrender/footprintpolicy.hpp"
#include <cassert>
#include <iostream>
#include <unordered_map>
namespace { using namespace MWRender::FootprintPolicy;
#include "apps/openmw/mwrender/footprintdata.inc"
}
int main()
{
    assert(baseName("Meshes\\R\\Guar.NIF") == "guar");
    assert(creatureTypes.at("guar") == "guar");
    assert(creatureTypes.at("rust rat") == "rat");
    assert(landSurfaces.at("_land_default") == 2);
    assert(modelSurfaces.size() > 2500);
    assert(modelSurfaces.at("meshes/1fh/1ex_entrance_molag.nif") == 2);
    assert(classify("tx_sand_01") == 2);
    assert(classify("tx_grass_01") == 1);
    assert(classify("tx_stone_01") == 0);
    assert(classify("tx_glass_01") == 0);
    assert(classify("tr_water00") == 0);
    assert(classify("lava_bright") == 0);
    assert(classify("lava_crust") == 2);
    assert(forbidden("meshes/stairs.nif", true));
    assert(!forbidden("meshes/stairs.nif", false));
    assert(forbidden("meshes/tree.nif", false));
    assert(!acceptStep(1.05, 1, 100, false)); // duplicate key
    assert(!acceptStep(2, 1, 0, false)); // stationary animation
    assert(acceptStep(2, 1, 100, false));
    assert(acceptStep(2, 1, 0, true)); // landing without horizontal movement
    assert(!validGround(0.1f, 0)); // wall
    assert(!validGround(1, -100)); // jumping above floor
    assert(validGround(0.8f, -2));
    assert(alpha(0, 60) == 1);
    assert(alpha(40, 60) == 1);
    assert(alpha(51, 60) == 0.5f);
    assert(alpha(60, 60) == 0);
    assert(alpha(61, 60) == 0);
    const auto zero = landIndex(0, 0);
    assert(zero.cellX == -1 && zero.x == 15 && zero.cellY == 0 && zero.y == 0);
    const auto negative = landIndex(-1, -1);
    assert(negative.cellX == -1 && negative.x == 14 && negative.cellY == -1 && negative.y == 15);
    for (int x = -25000; x < 25000; x += 131)
        for (int y = -17000; y < 17000; y += 511)
        {
            const auto i = landIndex(x, y);
            assert(i.x >= 0 && i.x < 16 && i.y >= 0 && i.y < 16);
        }
    std::cout << "PASS: surface/creature tables, path normalization, water/lava/stairs, duplicate steps, ground checks, fading and negative terrain coordinates\n";
}
