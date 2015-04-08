// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names/common.h"

#include "names/unotrie.h"
#include "script/names.h"
#include "util.h"
#include "utiltime.h"

bool fNameHistory = false;

/* ************************************************************************** */
/* CNameData.  */

void
CNameData::fromScript (unsigned h, const COutPoint& out,
                       const CNameScript& script)
{
  assert (script.isAnyUpdate ());
  value = script.getOpValue ();
  nHeight = h;
  prevout = out;
  addr = script.getAddress ();
}

/* ************************************************************************** */
/* CNameCache.  */

/* Try to get a name's associated data.  This looks only
   in entries, and doesn't care about deleted data.  */
bool
CNameCache::get (const valtype& name, CNameData& data) const
{
  const std::map<valtype, CNameData>::const_iterator i = entries.find (name);
  if (i == entries.end ())
    return false;

  data = i->second;
  return true;
}

bool
CNameCache::getHistory (const valtype& name, CNameHistory& res) const
{
  assert (fNameHistory);

  const std::map<valtype, CNameHistory>::const_iterator i = history.find (name);
  if (i == history.end ())
    return false;

  res = i->second;
  return true;
}

void
CNameCache::updateNamesForHeight (unsigned nHeight,
                                  std::set<valtype>& names) const
{
  /* Seek in the map of cached entries to the first one corresponding
     to our height.  */

  const ExpireEntry seekEntry(nHeight, valtype ());
  std::map<ExpireEntry, bool>::const_iterator it;

  for (it = expireIndex.lower_bound (seekEntry); it != expireIndex.end (); ++it)
    {
      const ExpireEntry& cur = it->first;
      assert (cur.nHeight >= nHeight);
      if (cur.nHeight > nHeight)
        break;

      if (it->second)
        names.insert (cur.name);
      else
        names.erase (cur.name);
    }
}

/* Insert (or update) a name.  If it is marked as "deleted", this also
   removes the "deleted" mark.  */
void
CNameCache::set (const valtype& name, const CNameData& data)
{
  const std::set<valtype>::iterator di = deleted.find (name);
  if (di != deleted.end ())
    deleted.erase (di);

  const std::map<valtype, CNameData>::iterator ei = entries.find (name);
  if (ei != entries.end ())
    ei->second = data;
  else
    entries.insert (std::make_pair (name, data));
}

void
CNameCache::setHistory (const valtype& name, const CNameHistory& data)
{
  assert (fNameHistory);

  const std::map<valtype, CNameHistory>::iterator ei = history.find (name);
  if (ei != history.end ())
    ei->second = data;
  else
    history.insert (std::make_pair (name, data));
}

/* Delete a name.  If it is in the "entries" set also, remove it there.  */
void
CNameCache::remove (const valtype& name)
{
  const std::map<valtype, CNameData>::iterator ei = entries.find (name);
  if (ei != entries.end ())
    entries.erase (ei);

  deleted.insert (name);
}

void
CNameCache::addExpireIndex (const valtype& name, unsigned height)
{
  const ExpireEntry entry(height, name);
  expireIndex[entry] = true;
}

void
CNameCache::removeExpireIndex (const valtype& name, unsigned height)
{
  const ExpireEntry entry(height, name);
  expireIndex[entry] = false;
}

/* Apply all the changes in the passed-in record on top of this one.  */

void
CNameCache::applyTo (CNameCache& cache) const
{
  for (std::map<valtype, CNameData>::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    cache.set (i->first, i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    cache.remove (*i);

  for (std::map<valtype, CNameHistory>::const_iterator i = history.begin ();
       i != history.end (); ++i)
    cache.setHistory (i->first, i->second);

  for (std::map<ExpireEntry, bool>::const_iterator i = expireIndex.begin ();
       i != expireIndex.end (); ++i)
    cache.expireIndex[i->first] = i->second;
}

void
CNameCache::applyTo (CUnoTrie& trie, bool expanded) const
{
#ifdef BENCHMARK_UNO_TRIE
  static int64_t nTotalMicros = 0;
  const int64_t nStart = GetTimeMicros ();
#endif // BENCHMARK_UNO_TRIE

  for (std::map<valtype, CNameData>::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    trie.Set (i->first.begin (), i->first.end (), i->second, expanded);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    trie.Delete (i->begin (), i->end (), expanded);

#ifdef BENCHMARK_UNO_TRIE
  /* Compute the root hash, since that is a step that a miner
     or full node has to do for every block.  This routine is called
     whenever pcoinsTip is updated from the temporary cache, which corresponds
     to the addition of a new block in the chain.  */
  trie.GetHash ();

  const int64_t nEnd = GetTimeMicros ();
  assert (nEnd >= nStart);
  nTotalMicros += nEnd - nStart;
  LogPrint ("bench", "UNO in-memory update: %.2fms, total %.3fs\n",
            (nEnd - nStart) * 1e-3, nTotalMicros * 1e-6);
#endif // BENCHMARK_UNO_TRIE
}
