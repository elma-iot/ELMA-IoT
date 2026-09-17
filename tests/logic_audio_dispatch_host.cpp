#include "logic_audio_dispatch.h"
#include <iostream>
#include <string>
#include <stdexcept>
#define CHECK(x) if(!(x))throw std::runtime_error(#x)
int main(){try{using namespace LogicAudioDispatch;
 auto a=token("group-A-play"),b=token("group-B-play");CHECK(valid("group-A-play",a));CHECK(valid("group-B-play",b));token("group-A-play",true);CHECK(!valid("group-A-play",a));CHECK(valid("group-B-play",b));auto fresh=token("group-A-play");CHECK(valid("group-A-play",fresh));CHECK(fresh!=a);
 auto dac=outputToken(false),buzzer=outputToken(true);outputToken(false,true);CHECK(outputToken(false)!=dac);CHECK(outputToken(true)==buzzer);
 for(int i=0;i<1000;++i)token(("replacement-"+std::to_string(i)).c_str());CHECK(!valid("group-A-play",fresh));CHECK(valid("",0));
 std::cout<<"PASS: stop invalidates only its queued source, new Play rearms, output cancellation is isolated, bounded owner cache rejects stale entries\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}}
