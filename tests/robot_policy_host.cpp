#include "robot_control_policy.h"
#include <cassert>
#include <climits>
int main(){using namespace RobotControlPolicy;
 assert(valid(-255,255,0,0,180));assert(!valid(INT_MIN,0,0,90,90));assert(!valid(0,256,0,90,90));assert(!valid(0,0,-256,90,90));assert(!valid(0,0,0,181,90));assert(!valid(0,0,0,90,-1));
 assert(!expired(349,0));assert(expired(350,0));assert(!expired(100,UINT_MAX-200));assert(expired(150,UINT_MAX-200));
 assert(ownedByOther(1,2,349,0));assert(!ownedByOther(1,2,350,0));assert(!ownedByOther(1,1,10,0));assert(!ownedByOther(0,2,10,0));
 for(int p:{16,17,18,19,43,44,0,20,21,1})assert(reservedPin(p));assert(!reservedPin(13));assert(!reservedPin(15));assert(!reservedPin(14));
}
