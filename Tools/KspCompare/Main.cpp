/*
    KspCompare: compares a project file as written (KspGen's Generated/ file, a backup or a project JSON) with a project read
    back from the KeyStep (KspBackup output), with the same rules as the read-back check of KspRestore / KspGen
    (ksp::writePlan applies the fix-ups first, ksp::compareReadBack lists what differs). No device, no JUCE.

        KspCompare written.json readback.json [--project N]     (N picks the project inside a backup file; default: the first)
*/

#include <fstream>
#include <iostream>
#include <sstream>
#include "Ksp/Restore.h"

namespace
{
std::string slurp (const char* path)
{
    std::ifstream f (path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const ksp::ProjectData* pick (const ksp::LoadResult& r, int project)
{
    if (! r.backup)
        return nullptr;
    for (const auto& p : r.backup->projects)
        if (project == 0 || p.project == project)
            return &p;
    return nullptr;
}
}

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: KspCompare written.json readback.json [--project N]" << std::endl;
        return 2;
    }
    int project = 0;
    for (int i = 3; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--project")
            project = std::atoi (argv[i + 1]);

    const auto a = ksp::fromJson (slurp (argv[1])), b = ksp::fromJson (slurp (argv[2]));
    const auto* wp = pick (a, project);
    const auto* rp = pick (b, project);
    if (wp == nullptr || rp == nullptr)
    {
        std::cout << "ERROR: " << (wp == nullptr ? a.error : b.error) << std::endl;
        return 2;
    }
    ksp::ProjectData written = *wp;
    written.project = rp->project;
    (void) ksp::writePlan (written);   // applies the fix-ups the read-back is compared against
    const auto problems = ksp::compareReadBack (written, *rp);
    std::cout << problems.size() << " value block(s) differ" << std::endl;
    for (const auto& p : problems)
        std::cout << "  " << p << std::endl;
    return problems.empty() ? 0 : 1;
}
