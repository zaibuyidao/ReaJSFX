// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

namespace modern {
inline std::wstring menuLiteral(const std::wstring& name) {
  std::wstring result;
  for (wchar_t ch:name) { result+=ch; if(ch==L'&')result+=ch; }
  return result;
}
inline bool libraryCandidate(const std::filesystem::path& path) {
  auto extension=path.extension().wstring();
  std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
  // Same exclusions as official ReaJS CreateMenuForPath. A desc: is optional.
  for(auto excluded:{L".png",L".dll",L".jsfx-inc",L".jpg",L".zip",L".exe",
      L".dat",L".bmp",L".rpl",L".db",L".wav",L".ogg"})
    if(extension==excluded)return false;
  return true;
}
inline HMENU makeLibraryMenu(const std::filesystem::path& folder,
    std::vector<std::string>& files,std::vector<std::filesystem::path>& ancestors) {
  std::error_code ec;
  auto canonical=std::filesystem::weakly_canonical(folder,ec);
  if(ec||std::find(ancestors.begin(),ancestors.end(),canonical)!=ancestors.end())return nullptr;
  ancestors.push_back(canonical);
  std::vector<std::filesystem::directory_entry> entries;
  for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec))
    entries.push_back(*it);
  std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){
    std::error_code ea,eb;const bool da=a.is_directory(ea),db=b.is_directory(eb);
    if(da!=db)return da;
    return a.path().filename()<b.path().filename();
  });
  HMENU menu=CreatePopupMenu();
  for(const auto& entry:entries) {
    const auto name=entry.path().filename().wstring();
    if(name.empty()||name[0]==L'.')continue;
    const auto label=menuLiteral(name);
    ec.clear();
    if(entry.is_directory(ec)) {
      if(auto sub=makeLibraryMenu(entry.path(),files,ancestors))
        AppendMenuW(menu,MF_POPUP,(UINT_PTR)sub,label.c_str());
    } else if(entry.is_regular_file(ec)&&libraryCandidate(entry.path())) {
      files.push_back(entry.path().u8string());
      AppendMenuW(menu,MF_STRING,1000+files.size(),label.c_str());
    }
  }
  ancestors.pop_back();
  if(!GetMenuItemCount(menu)){DestroyMenu(menu);return nullptr;}
  return menu;
}
inline HMENU makeLibraryMenu(const std::filesystem::path& folder,std::vector<std::string>& files) {
  std::vector<std::filesystem::path> ancestors;
  return makeLibraryMenu(folder,files,ancestors);
}
}
