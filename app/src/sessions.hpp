// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core.hpp"
namespace amalthea {
class SessionBank {
public:
 explicit SessionBank(std::string legacyPath):legacy(std::move(legacyPath)){}
 std::string path(int slot)const;
 std::array<uint8_t,64> scan()const;
 bool save(int slot,const Editor &,std::string &error)const;
 bool load(int slot,Editor &,std::string &error)const;
 bool save(int slot,const Project &,std::string &error)const;
 bool load(int slot,Project &,std::string &error)const;
private:
 std::string legacy;
 std::string source(int slot)const;
};
}
