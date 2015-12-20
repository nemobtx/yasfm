#include "matching.h"

#include <ctime>
#include <iostream>

using std::cerr;
using std::cout;

namespace yasfm
{

void removePoorlyMatchedPairs(int minNumMatches,pair_umap<CameraPair> *pairs)
{
  for(auto it = pairs->begin(); it != pairs->end();)
  {
    auto &pair = it->second;
    if(pair.matches.size() < minNumMatches)
    {
      it = pairs->erase(it);
    } else
    {
      ++it;
    }
  }
}

bool OptionsFLANN::filterByRatio() const { return ratioThresh >= 0.f; }

void OptionsFLANN::write(ostream& file) const
{
  file << " indexParams:\n";
  for(const auto& entry : indexParams)
  {
    file << "  " << entry.first << ": " << entry.second << "\n";
  }
  file << " searchParams:\n";
  file << "  checks: " << searchParams.checks << "\n";
  file << "  eps: " << searchParams.eps << "\n";
  file << "  sorted: " << searchParams.sorted << "\n";
  file << "  max_neighbors: " << searchParams.max_neighbors << "\n";
  file << "  cores: " << searchParams.cores << "\n";
  file << " ratioThresh: " << ratioThresh << "\n";
  file << " onlyUniques: " << onlyUniques << "\n";
  file << " verbose: " << verbose << "\n";
}

void matchFeatFLANN(const OptionsFLANN& opt,const ptr_vector<Camera>& cams,
  pair_umap<CameraPair> *pairs)
{
  vector<set<int>> queries;
  int nImages = static_cast<int>(cams.size());
  queries.resize(nImages);
  for(int i = 0; i < nImages; i++)
  {
    for(int j = i + 1; j < nImages; j++)
    {
      queries[j].insert(i);
    }
  }
  matchFeatFLANN(opt,cams,queries,pairs);
}

void matchFeatFLANN(const OptionsFLANN& opt,const ptr_vector<Camera>& cams,
  const vector<set<int>>& queries,pair_umap<CameraPair> *pairs)
{
  vector<flann::Matrix<float>> descr;
  descr.reserve(cams.size());
  for(const auto& pcam : cams)
  {
    const auto& curr = pcam->descr();
    // cast away const-ness because of compability with flann and
    // switch to row major and transpose (=exchange nrows and ncols)
    descr.emplace_back(const_cast<float*>(curr.data()),curr.cols(),curr.rows());
  }

  matchFeatFLANN(opt,descr,queries,pairs);
}

void matchFeatFLANN(const OptionsFLANN& opt,const vector<flann::Matrix<float>>& descr,
  const vector<set<int>>& queries,pair_umap<CameraPair> *ppairs)
{
  auto& pairs = *ppairs;
  size_t sz = 0;
  for(const auto& entry : queries)
  {
    sz += entry.size();
  }
  pairs.reserve(sz);

  clock_t start,end;
  int numQueries = static_cast<int>(queries.size());
  for(int j = 0; j < numQueries; j++)
  {
    if(queries[j].empty())
      continue; // no reason to build the trees
    flann::Index<flann::L2<float>> index(descr[j],opt.indexParams);
    index.buildIndex();
    for(int i : queries[j])
    {
      if(opt.verbose)
      {
        cout << "matching: " << i << " -> " << j << "\t";
        start = clock();
      }
      IntPair pairIdx(i,j);
      matchFeatFLANN(opt,index,descr[i],&pairs[pairIdx]);
      if(opt.verbose)
      {
        end = clock();
        cout << "found " << pairs[pairIdx].matches.size() << " matches" << "\t";
        cout << "took: " << (double)(end - start) / (double)CLOCKS_PER_SEC << "s\n";
      }
    }
  }
}

void matchFeatFLANN(const OptionsFLANN& opt,const flann::Index<flann::L2<float>>& index,
  const flann::Matrix<float>& queryDescr,CameraPair *pair)
{
  int numQueries = static_cast<int>(queryDescr.rows);
  AutoMemReleaseFlannMatrix<size_t> nearestNeighbors(numQueries,2);
  AutoMemReleaseFlannMatrix<float> dists(numQueries,2);
  auto& outMatches = pair->matches;
  auto& outDists = pair->dists;

  if(opt.filterByRatio())
  {
    float sqThresh = opt.ratioThresh*opt.ratioThresh; // flann returns squared distances
    index.knnSearch(queryDescr,nearestNeighbors.data,dists.data,2,opt.searchParams);
    for(int i = 0; i < numQueries; i++)
    {
      // Ratio of the distance to the nearest neighbor over the distance 
      // to the second nearest neighbor
      float ratio = (dists.data[i][0] / dists.data[i][1]);
      if(ratio < sqThresh)
      {
        outMatches.emplace_back(i,static_cast<int>(nearestNeighbors.data[i][0]));
        outDists.push_back(dists.data[i][0]);
      }
    }
  } else
  {
    outMatches.reserve(numQueries);
    outDists.reserve(numQueries);
    index.knnSearch(queryDescr,nearestNeighbors.data,dists.data,1,opt.searchParams);
    for(int i = 0; i < numQueries; i++)
    {
      outMatches.emplace_back(i,static_cast<int>(nearestNeighbors.data[i][0]));
      outDists.push_back(dists.data[i][0]);
    }
  }

  vector<bool> unique; // empty means that the unique option is turned off
  if(opt.onlyUniques)
  {
    findUniqueMatches(outMatches,index.size(),&unique);
  }

  if(!unique.empty())
  {
    filterVector(unique,&outMatches);
    filterVector(unique,&outDists);
  }
}

void findUniqueMatches(const vector<IntPair>& matches,size_t numFeats2,
  vector<bool> *puniqueMatches)
{
  auto& uniqueMatches = *puniqueMatches;
  // -1 means that a feature from feats2 was not matched to any from feats1
  vector<int> target2match(numFeats2,-1);

  int numMatches = static_cast<int>(matches.size());
  uniqueMatches.resize(numMatches,true);
  for(int matchIdx = 0; matchIdx < numMatches; matchIdx++)
  {
    int prevMatch = target2match[matches[matchIdx].second];
    if(prevMatch == -1)
    {
      target2match[matches[matchIdx].second] = matchIdx;
    } else
    {
      uniqueMatches[matchIdx] = false;
      uniqueMatches[prevMatch] = false;
    }
  }
}

} // namespace yasfm