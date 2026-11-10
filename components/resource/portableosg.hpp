#ifndef OPENMW_RESOURCE_PORTABLEOSG_HPP
#define OPENMW_RESOURCE_PORTABLEOSG_HPP

// Y111: dlopen image loaders are not part of the executable's DT_NEEDED list.
// Give Linux portable builds their own OSG-version-matched search directories,
// including when the ELF is executed directly, from another working directory.
#if defined(__linux__) && !defined(ANDROID)
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <osg/Image>
#include <osg/ref_ptr>
#include <osg/Version>
#include <osgDB/Registry>

namespace Resource::PortableOsg
{
    namespace fs = std::filesystem;

    inline fs::path portableRoot()
    {
        std::error_code error;
        // /proc identifies the actual ELF and survives symlinks/PATH launching.
        const fs::path executable = fs::read_symlink("/proc/self/exe", error);
        if (!error)
        {
            const fs::path root = executable.parent_path();
            if (fs::is_directory(root / "lib", error)
                && fs::is_directory(root / "resources", error))
                return root;
        }
        if (const char* value = std::getenv("ARENAMP_PORTABLE_ROOT"))
        {
            const fs::path root(value);
            if (root.is_absolute() && fs::is_directory(root / "lib", error))
                return root;
        }
        return {};
    }

    inline std::vector<fs::path> pluginPaths(const fs::path& root, const std::string& version)
    {
        if (root.empty()) return {};
        const std::string versioned = "osgPlugins-" + version;
        const fs::path candidates[] = {root / "lib" / versioned, root / "lib64" / versioned,
            root / "lib" / "osgPlugins", root / versioned, root / "osgPlugins"};
        std::vector<fs::path> paths;
        for (const auto& candidate : candidates)
        {
            std::error_code error;
            if (fs::is_directory(candidate, error)) paths.push_back(candidate);
        }
        return paths;
    }

    inline void configure()
    {
        const auto paths = pluginPaths(portableRoot(), osgGetVersion());
        auto& search = osgDB::Registry::instance()->getLibraryFilePathList();
        for (auto i = paths.rbegin(); i != paths.rend(); ++i)
        {
            const std::string path = i->string();
            search.erase(std::remove(search.begin(), search.end(), path), search.end());
            search.insert(search.begin(), path);
        }
        for (const auto& path : paths)
            std::cerr << "ArenaMP OSG plugin path: " << path.string() << '\n';
    }

    inline std::string tgaReadFixture()
    {
        // Y112: the OSG 3.6.5 reader checks a 26-byte footer even for old TGAs.
        // Its 1x1 RGB writer emits only 21 bytes. Test the game's read path with
        // a known uncompressed 2x2 TGA and a complete TGA 2.0 footer instead.
        constexpr unsigned char bytes[] = {
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0x20,
            50, 100, 200, 50, 100, 200, 50, 100, 200, 50, 100, 200,
            0, 0, 0, 0, 0, 0, 0, 0,
            'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O', 'N', '-', 'X', 'F', 'I', 'L', 'E', '.', 0
        };
        return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }

    inline bool checkImageCodecs()
    {
        bool ok = true;
        const auto root = portableRoot();
        const auto paths = pluginPaths(root, osgGetVersion());
        auto* registry = osgDB::Registry::instance();
        std::cerr << "ArenaMP image codec check, OSG " << osgGetVersion() << '\n';
        // Absolute preloads prevent an incomplete archive passing this check
        // by silently using a build host's installed OSG plugins.
        if (!root.empty())
        {
            for (const char* name : {"dds", "png", "tga", "jpeg", "bmp", "freetype", "osg", "serializers_osg"})
            {
                bool loaded = false;
                for (const auto& path : paths)
                {
                    const auto plugin = path / (std::string("osgdb_") + name + ".so");
                    std::error_code error;
                    if (!fs::is_regular_file(plugin, error)) continue;
                    if (registry->loadLibrary(plugin.string()) != osgDB::Registry::NOT_LOADED)
                    {
                        loaded = true;
                        break;
                    }
                }
                if (!loaded)
                {
                    std::cerr << "FAIL bundled OSG plugin: osgdb_" << name
                              << ".so (missing or a runtime dependency cannot load)\n";
                    ok = false;
                }
            }
        }

        // Encode/decode PNG (including map writing), DDS, BMP and JPEG, and
        // decode a valid TGA fixture, without a window, GPU or game data.
        // This checks real plugin calls, not just the presence of .so files.
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
        image->data()[0] = 200; image->data()[1] = 100; image->data()[2] = 50;
        for (const char* extension : {"dds", "png", "tga", "bmp", "jpg"})
        {
            auto* reader = registry->getReaderWriterForExtension(extension);
            if (!reader)
            {
                std::cerr << "FAIL image reader: " << extension << '\n';
                ok = false;
                continue;
            }
            int expectedSize = 1;
            std::string data;
            if (std::string(extension) == "tga")
            {
                expectedSize = 2;
                data = tgaReadFixture();
            }
            else
            {
                std::ostringstream encoded(std::ios::out | std::ios::binary);
                const auto written = reader->writeImage(*image, encoded);
                if (!written.success())
                {
                    std::cerr << "FAIL image write: " << extension << ": " << written.message() << '\n';
                    ok = false;
                    continue;
                }
                data = encoded.str();
            }
            std::istringstream stream(data, std::ios::in | std::ios::binary);
            auto result = reader->readImage(stream);
            const osg::Image* decoded = result.getImage();
            if (!result.success() || !decoded || decoded->s() != expectedSize || decoded->t() != expectedSize)
            {
                std::cerr << "FAIL image read: " << extension << ": " << result.message()
                          << " status=" << result.status() << " bytes=" << data.size();
                if (decoded) std::cerr << " size=" << decoded->s() << 'x' << decoded->t();
                std::cerr << '\n';
                ok = false;
                continue;
            }
            std::cerr << "PASS image codec: " << extension << '\n';
        }
        return ok;
    }
}
#endif
#endif
