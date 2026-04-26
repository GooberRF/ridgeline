#include <ridgeline/Module.h>

namespace ridgeline {

ModuleRegistry& ModuleRegistry::instance()
{
    static ModuleRegistry s;
    return s;
}

void ModuleRegistry::register_module(std::unique_ptr<IModule> module)
{
    m_view.push_back(module.get());
    m_modules.push_back(std::move(module));
}

std::span<IModule* const> ModuleRegistry::all() const
{
    return {m_view.data(), m_view.size()};
}

} // namespace ridgeline
