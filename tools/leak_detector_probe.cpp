// Calibration target only; never link this intentional leak into the product.
#include <cstdlib>
#include <cstring>

// The lost allocation is intentional: this validates the detector, not the app.
// NOLINTBEGIN(clang-analyzer-unix.Malloc)
int main(int argc, char **argv)
{
    const bool leak = argc == 2 && std::strcmp(argv[1], "--leak") == 0;
    if (argc != 2 || (!leak && std::strcmp(argv[1], "--clean") != 0))
        return 2;
    auto *allocation = static_cast<unsigned char *>(std::malloc(4096));
    if (!allocation)
        return 3;
    auto *bytes = static_cast<volatile unsigned char *>(allocation);
    for (int index = 0; index < 4096; ++index)
        bytes[index] = static_cast<unsigned char>(index);
    if (!leak)
        std::free(allocation);
    return 0;
}
// NOLINTEND(clang-analyzer-unix.Malloc)
