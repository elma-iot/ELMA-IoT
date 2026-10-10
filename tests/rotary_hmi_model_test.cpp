#include "rotary_hmi_model.h"
#include <cassert>
using namespace RotaryHmi;
int main(){
    HoldFailsafe hold;assert(!hold.sample(true,100));assert(!hold.sample(true,15099));assert(hold.sample(true,15100));assert(!hold.sample(true,16000));
    assert(!hold.sample(false,16001));assert(!hold.sample(true,17000));assert(!hold.sample(false,18000));assert(!hold.sample(true,20000));assert(!hold.sample(true,34999));assert(hold.sample(true,35000));
    HoldFailsafe wrapped;assert(!wrapped.sample(true,UINT32_MAX-10000));assert(!wrapped.sample(true,4998));assert(wrapped.sample(true,4999));
    Encoder e;e.reset(0,0);int steps=0;
    for(int state:{2,3,1,0})steps+=e.sample(state&2,state&1);assert(steps==1);
    for(int state:{1,3,2,0})steps+=e.sample(state&2,state&1);assert(steps==0);
    for(int state:{2,0,2,0,2,0})assert(e.sample(state&2,state&1)==0); // bounce cancels
    e.reset(0,0);assert(e.sample(1,1)==0);assert(e.partial==0); // impossible jump
    // MD80E detents are half-cycles: each two-edge click must move once.
    e.reset(0,0);
    assert(e.sample(1,0,2)==0);assert(e.sample(1,1,2)==1);
    assert(e.sample(0,1,2)==0);assert(e.sample(0,0,2)==1);
    assert(e.sample(0,1,2)==0);assert(e.sample(1,1,2)==-1);
    assert(e.sample(1,0,2)==0);assert(e.sample(0,0,2)==-1);
    e.reset(0,0);assert(e.sample(1,0,2)==0);assert(e.sample(0,0,2)==0);
    Model m;Item value;value.id=1;value.title="Value";value.kind="value";value.value=50;
    Item submenu;submenu.id=2;submenu.kind="menu";submenu.submenu=1;
    Item confirm;confirm.id=3;confirm.parent=1;confirm.kind="confirm";
    m.items={value,submenu,confirm};assert(!m.activate());assert(m.editing);assert(m.navigate(1000));assert(m.current()->value==100);
    m.back();assert(!m.editing);m.navigate(-1);assert(m.current()->id==2);m.activate();assert(m.menu==1);
    assert(!m.activate());assert(m.confirming);assert(m.activate());assert(!m.confirming);m.back();assert(m.menu==0);
    assert(!m.setValue(1,NAN));assert(m.setValue(1,-5));assert(m.items[0].value==0);assert(!m.select(3));
}
