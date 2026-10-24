#pragma once
#include "sample.h"
#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
namespace whic {
inline void autoMapSamples(std::vector<Sample>& samples){
 for(auto& s:samples){s.map=inferMapping(s.path);auto name=std::filesystem::path(s.path).stem().wstring();std::transform(name.begin(),name.end(),name.begin(),towlower);s.releaseTrigger=std::regex_search(name,std::wregex(L"(?:^|[_ -])(release|rel)(?:$|[_ -])"));std::wsmatch m;if(s.map.velLow==1&&s.map.velHigh==127&&std::regex_search(name,m,std::wregex(L"(?:^|[_ -])v(?:el)?([0-9]{1,3})(?:$|[_ -])"))){s.map.velLow=s.map.velHigh=std::clamp(std::stoi(m[1]),1,127);}}
 for(auto& s:samples){std::set<int> roots,vels;for(auto& other:samples)if(other.releaseTrigger==s.releaseTrigger){roots.insert(other.map.root);if(other.map.root==s.map.root)vels.insert(other.map.velLow==other.map.velHigh?other.map.velLow:64);}auto pos=roots.find(s.map.root);s.map.low=pos==roots.begin()?21:(*std::prev(pos)+s.map.root)/2+1;auto next=std::next(pos);s.map.high=next==roots.end()?108:(s.map.root+*next)/2;s.map.low=std::min(s.map.low,s.map.root);s.map.high=std::max(s.map.high,s.map.root);}
 // Snapshot velocity anchors before expanding them, avoiding order-dependent layer boundaries.
 std::vector<Mapping> anchors;for(auto& s:samples)anchors.push_back(s.map);
 for(size_t i=0;i<samples.size();++i){auto& s=samples[i];if(anchors[i].velLow!=anchors[i].velHigh)continue;int lo=0,hi=128,v=anchors[i].velLow;for(size_t j=0;j<samples.size();++j)if(samples[j].releaseTrigger==s.releaseTrigger&&anchors[j].root==s.map.root&&anchors[j].velLow==anchors[j].velHigh){int q=anchors[j].velLow;if(q<v)lo=std::max(lo,q);if(q>v)hi=std::min(hi,q);}s.map.velLow=lo?(lo+v)/2+1:1;s.map.velHigh=hi==128?127:(v+hi)/2;}
}
}
