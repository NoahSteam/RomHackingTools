/* Saturn Explorer — the no-throw boundary both seam shims sit behind.
 *
 * This is not part of the ABI. It is the one piece of C++ every exported
 * extern "C" function needs, and it lives beside the seam headers so that
 * whoever adds an entry point finds it in the same place as the contract it
 * implements (SeHost.h, "Exceptions"; SeDataSource.h for the driver side).
 *
 * The rule: no exception may leave an se_* function. The caller can be C, or
 * JavaScript in the web build, so there is no frame on the other side that
 * could catch one and unwinding past it is undefined -- and the C++ frontend
 * installs no handler either, so a throw that escapes is a crash there too.
 * std::bad_alloc is the reachable case, and it needs no bug: a host low on
 * memory, a 64 MiB savestate, a dimension read out of a corrupt register.
 *
 * Guard turns that into the answer the function already has for "cannot supply
 * this": a result code, a zero count, a null handle. Wrap every entry point,
 * not only the ones whose call graph allocates today -- the narrower rule
 * stops being true the first time the code below it grows an allocation, and
 * nothing re-derives it.
 */
#ifndef SATURNEXPLORER_SE_GUARD_H
#define SATURNEXPLORER_SE_GUARD_H

#ifdef __cplusplus

namespace se
{

template <typename Fallback, typename Body>
auto Guard(Fallback onThrow, Body body) -> decltype(body())
{
    try
    {
        return body();
    }
    catch (...)
    {
        return onThrow;
    }
}

}  // namespace se

#endif /* __cplusplus */

#endif /* SATURNEXPLORER_SE_GUARD_H */
