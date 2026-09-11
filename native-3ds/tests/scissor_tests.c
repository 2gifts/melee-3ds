#include <assert.h>
#include <stdio.h>
#include "../port/3ds/scissor.h"
int main(void){
    unsigned seed=1234567;
    for(unsigned test=0;test<30000;++test){
        unsigned q[4],r[4];
        for(unsigned k=0;k<4;++k){seed=seed*1664525u+1013904223u;q[k]=(seed>>12)%900;}
        int visible=mp_scissor_rect(q[0],q[1],q[2],q[3],r),xs=0,ys=0;
        for(unsigned j=0;j<400;++j){
            unsigned gx5=(799-2*j)*4;
            int inside=gx5>=q[0]*5&&gx5<(q[0]+q[2])*5;
            assert(inside==(j>=r[1]&&j<r[3]));xs+=inside;
        }
        for(unsigned i=0;i<240;++i){
            unsigned gy=479-2*i;
            int inside=gy>=q[1]&&gy<q[1]+q[3];
            assert(inside==(i>=r[0]&&i<r[2]));ys+=inside;
        }
        assert(visible==(xs!=0&&ys!=0));
        visible=mp_scissor_rect_width(q[0],q[1],q[2],q[3],320,r);xs=0;
        for(unsigned j=0;j<400;++j){
            int gx=719-(int)j*2;
            int inside=j>=40&&j<360&&gx>=(int)q[0]&&gx<(int)(q[0]+q[2]);
            assert(inside==(j>=r[1]&&j<r[3]));xs+=inside;
        }
        assert(visible==(xs!=0&&ys!=0));
    }
    unsigned r[4];assert(mp_scissor_rect(0,0,640,480,r));
    assert(r[0]==0&&r[1]==0&&r[2]==240&&r[3]==400);
    assert(!mp_scissor_rect(~0u,~0u,~0u,~0u,r));
    assert(mp_scissor_rect_width(0,0,640,480,320,r));
    assert(r[0]==0&&r[1]==40&&r[2]==240&&r[3]==360);
    assert(!mp_scissor_rect_width(~0u,~0u,~0u,~0u,320,r));
    puts("60,000 4:3/full-screen camera rectangles matched pixel-center reference");
}
