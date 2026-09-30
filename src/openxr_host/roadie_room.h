#pragma once
#include <DirectXMath.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace blvr_xr_host {
// Static, metre-scale scenery. All coordinates belong to the recentered room,
// never to an individual eye or the moving player's navigation transform.
struct RoomVertex {
    DirectX::XMFLOAT3 p,n;
    DirectX::XMFLOAT2 uv;
    DirectX::XMFLOAT4 weights;
    uint32_t joints[4];
};
struct RoadieRoom {
    std::vector<RoomVertex> vertices;
    std::vector<uint32_t> indices;
    enum Surface : unsigned { Stone, Floor, Iron, Brass, Speaker, Ember, Sky, Fire, Cloth };

    void triangle(DirectX::XMFLOAT3 a,DirectX::XMFLOAT3 b,DirectX::XMFLOAT3 c,
                  DirectX::XMFLOAT3 tint,unsigned material) {
        using namespace DirectX;
        XMFLOAT3 normal;
        XMStoreFloat3(&normal,XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&b)-XMLoadFloat3(&a),XMLoadFloat3(&c)-XMLoadFloat3(&a))));
        const auto first=static_cast<uint32_t>(vertices.size());
        for(auto p:{a,b,c})vertices.push_back({p,normal,{p.x,p.z},{tint.x,tint.y,tint.z,1},{material,0,0,0}});
        indices.insert(indices.end(),{first,first+1,first+2});
    }
    void face(DirectX::XMFLOAT3 a,DirectX::XMFLOAT3 b,DirectX::XMFLOAT3 c,DirectX::XMFLOAT3 d,
              DirectX::XMFLOAT3 tint,unsigned material) {
        triangle(a,b,c,tint,material);triangle(a,c,d,tint,material);
    }
    void box(float x,float y,float z,float sx,float sy,float sz,DirectX::XMFLOAT3 tint,unsigned material,float yaw=0) {
        using namespace DirectX;
        XMFLOAT3 p[8];
        const XMMATRIX transform=XMMatrixRotationY(yaw)*XMMatrixTranslation(x,y,z);
        for(unsigned i=0;i<8;++i)XMStoreFloat3(&p[i],XMVector3TransformCoord(XMVectorSet(i&1?sx/2:-sx/2,i&2?sy/2:-sy/2,i&4?sz/2:-sz/2,1),transform));
        face(p[0],p[2],p[3],p[1],tint,material);face(p[5],p[7],p[6],p[4],tint,material);
        face(p[4],p[6],p[2],p[0],tint,material);face(p[1],p[3],p[7],p[5],tint,material);
        face(p[2],p[6],p[7],p[3],tint,material);face(p[4],p[0],p[1],p[5],tint,material);
    }
    void column(float x,float y,float z,float r0,float r1,float height,unsigned sides,
                 DirectX::XMFLOAT3 tint,unsigned material,float leanX=0,float leanZ=0) {
        constexpr float tau=6.28318530718f;
        for(unsigned i=0;i<sides;++i) {
            const float a=tau*static_cast<float>(i)/static_cast<float>(sides),b=tau*static_cast<float>(i+1)/static_cast<float>(sides);
            DirectX::XMFLOAT3 p{x+r0*std::cos(a),y,z+r0*std::sin(a)},q{x+r0*std::cos(b),y,z+r0*std::sin(b)};
            DirectX::XMFLOAT3 u{x+leanX+r1*std::cos(a),y+height,z+leanZ+r1*std::sin(a)},v{x+leanX+r1*std::cos(b),y+height,z+leanZ+r1*std::sin(b)};
            face(q,p,u,v,tint,material);
            if(r1>0)triangle({x+leanX,y+height,z+leanZ},v,u,tint,material);
        }
    }
    void ring(float x,float y,float z,float inside,float outside,DirectX::XMFLOAT3 tint,unsigned material) {
        constexpr unsigned segments=96;
        for(unsigned i=0;i<segments;++i) {
            const float a=6.28318530718f*static_cast<float>(i)/segments,b=6.28318530718f*static_cast<float>(i+1)/segments;
            face({x+inside*std::cos(a),y,z+inside*std::sin(a)},
                 {x+inside*std::cos(b),y,z+inside*std::sin(b)},
                 {x+outside*std::cos(b),y,z+outside*std::sin(b)},
                 {x+outside*std::cos(a),y,z+outside*std::sin(a)},tint,material);
        }
    }
    void speaker(float x,float z,float yaw) {
        using namespace DirectX;
        const XMMATRIX transform=XMMatrixRotationY(yaw)*XMMatrixTranslation(x,0,z);
        auto part=[&](float px,float py,float pz,float sx,float sy,float sz,XMFLOAT3 color,unsigned surface) {
            XMFLOAT3 p;XMStoreFloat3(&p,XMVector3TransformCoord(XMVectorSet(px,py,pz,1),transform));
            box(p.x,p.y,p.z,sx,sy,sz,color,surface,yaw);
        };
        part(0,1.42f,0,1.45f,2.7f,.85f,{.075f,.07f,.063f},Iron);
        for(float side:{-1.f,1.f})part(side*.7f,1.42f,.45f,.085f,2.76f,.1f,{.30f,.22f,.12f},Brass);
        for(float h:{.11f,1.43f,2.77f})part(0,h,.46f,1.4f,.06f,.1f,{.30f,.22f,.12f},Brass);
        // Recessed speaker cones are solid geometry, with grille shading.
        for(float h:{.75f,2.05f})for(float s:{-1.f,1.f}) {
            constexpr unsigned segments=24;
            XMFLOAT3 center;XMStoreFloat3(&center,XMVector3TransformCoord(XMVectorSet(s*.33f,h,.48f,1),transform));
            for(unsigned i=0;i<segments;++i) {
                const float a=6.28318530718f*static_cast<float>(i)/segments,b=6.28318530718f*static_cast<float>(i+1)/segments;
                XMFLOAT3 p,q;XMStoreFloat3(&p,XMVector3TransformCoord(XMVectorSet(s*.33f+.29f*std::cos(a),h+.29f*std::sin(a),.51f,1),transform));
                XMStoreFloat3(&q,XMVector3TransformCoord(XMVectorSet(s*.33f+.29f*std::cos(b),h+.29f*std::sin(b),.51f,1),transform));
                triangle(center,p,q,{.22f,.20f,.17f},Speaker);
            }
        }
        part(0,2.99f,0,1.28f,.35f,.73f,{.15f,.12f,.085f},Iron);
        for(int i=0;i<7;++i)part(-.49f+.15f*static_cast<float>(i),3.f,.38f,.035f,.04f,.018f,{.8f,.27f,.04f},Ember);
    }
    RoadieRoom() {
        using DirectX::XMFLOAT3;
        const XMFLOAT3 basalt{.19f,.215f,.24f},iron{.13f,.145f,.16f},brass{.40f,.235f,.085f};
        // Broad stone terrace, a recessed centre medallion and inlaid perimeter.
        face({-25,0,25},{25,0,25},{25,0,-25},{-25,0,-25},{.22f,.23f,.25f},Floor);
        ring(0,.008f,0,2.02f,2.10f,brass,Brass);
        ring(0,.009f,0,2.15f,2.18f,iron,Iron);
        ring(0,.007f,0,6.65f,6.82f,brass,Brass);
        for(int i=0;i<24;++i) {
            const float a=6.2831853f*static_cast<float>(i)/24;
            const float x=6.76f*std::sin(a),z=6.76f*std::cos(a);
            column(x,0,z,.075f,.065f,.65f,6,iron,Iron);
            column(x,.65f,z,.09f,0,.19f,5,brass,Brass);
            for(int segment=0;segment<4;++segment) {
                const float next=a+(static_cast<float>(segment)+.5f)*6.2831853f/96;
                const float drop=.13f*std::sin((static_cast<float>(segment)+.5f)*3.14159265f/4);
                box(6.76f*std::sin(next),.56f-drop,6.76f*std::cos(next),.45f,.035f,.035f,iron,Iron,next);
            }
        }
        // Screen is mounted in an iron stage arch, with a physical footswitch.
        box(0,1.99f,-3.28f,3.54f,2.02f,.27f,iron,Iron);
        for(float side:{-1.f,1.f}) {
            box(side*1.74f,2.08f,-3.07f,.15f,2.18f,.22f,brass,Brass);
            box(side*1.90f,1.52f,-3.35f,.36f,3.04f,.48f,basalt,Stone);
            column(side*1.9f,3.02f,-3.35f,.25f,0,1.55f,5,iron,Iron,side*.52f,-.3f);
            // Sweeping metal shoulder fins behind the display.
            for(int i=0;i<4;++i) {
                const float f=static_cast<float>(i);
                triangle({side*(1.4f+f*.23f),3.10f-f*.12f,-3.38f},
                         {side*(2.95f+f*.30f),3.75f-f*.28f,-3.62f},
                         {side*(1.85f+f*.28f),2.91f-f*.17f,-3.4f},brass,Brass);
            }
        }
        for(float h:{1.09f,3.06f})box(0,h,-3.07f,3.55f,.12f,.23f,brass,Brass);
        box(0,.50f,-3.4f,1.23f,1.f,.62f,iron,Iron);
        box(0,1.04f,-3.08f,1.05f,.41f,.20f,brass,Brass);
        // Surrounding gear sits outside the clear walking circle.
        for(float side:{-1.f,1.f}) {
            const float bannerX=side*4.2f,bannerZ=-5.8f;
            column(bannerX,0,bannerZ,.07f,.055f,4.8f,8,iron,Iron);
            column(bannerX,4.8f,bannerZ,.13f,0,.55f,5,brass,Brass);
            box(bannerX,4.5f,bannerZ,1.65f,.065f,.065f,brass,Brass);
            for(int stripe=0;stripe<8;++stripe) {
                const float u=static_cast<float>(stripe)/8,v=static_cast<float>(stripe+1)/8;
                const float x0=bannerX+(u-.5f)*1.5f,x1=bannerX+(v-.5f)*1.5f;
                const float z0=bannerZ+.08f+.09f*std::sin(u*12),z1=bannerZ+.08f+.09f*std::sin(v*12);
                const float low0=2.0f+((stripe%3)==0?.13f:0),low1=2.0f+(((stripe+1)%3)==0?.13f:0);
                face({x0,low0,z0},{x1,low1,z1},{x1,4.47f,z1},{x0,4.47f,z0},{.23f,.018f,.012f},Cloth);
            }
            // Angular brass wing crest, without lettering or a placard.
            for(float wing:{-1.f,1.f})for(int feather=0;feather<3;++feather) {
                const float f=static_cast<float>(feather);
                triangle({bannerX,3.0f+f*.15f,bannerZ+.23f},
                         {bannerX+wing*(.6f-f*.06f),3.7f+f*.13f,bannerZ+.23f},
                         {bannerX+wing*.12f,2.75f+f*.20f,bannerZ+.23f},brass,Brass);
            }
            speaker(side*6.6f,-3.1f,side*-.42f);
            speaker(side*7.5f,-.1f,side*-.72f);
            box(side*7.7f,.5f,3.4f,1.7f,1.f,.95f,{.14f,.11f,.08f},Iron,side*.15f);
            for(float h:{.09f,.93f})box(side*7.7f,h,3.4f,1.74f,.06f,.98f,brass,Brass,side*.15f);
            for(float z:{-4.8f,4.8f}) {
                const float x=side*5.5f;
                column(x,0,z,.46f,.32f,.85f,8,basalt,Stone);
                column(x,.82f,z,.28f,.53f,.32f,8,iron,Iron);
                column(x,1.14f,z,.40f,.40f,.025f,12,{.7f,.16f,.014f},Ember);
                for(int i=0;i<5;++i) {
                    const float a=6.2831853f*static_cast<float>(i)/5;
                    column(x+.2f*std::sin(a),1.16f,z+.2f*std::cos(a),.13f,0,.36f+.17f*std::sin(a+2),5,{1,.35f,.035f},Fire,.055f*std::cos(a),0);
                }
            }
        }
        // Chipped basalt buttresses and a monumental rear wall anchor the space.
        for(int i=0;i<14;++i) {
            const float a=6.2831853f*static_cast<float>(i)/14;
            const float x=9.3f*std::sin(a),z=9.3f*std::cos(a);
            const float h=3.9f+1.25f*std::sin(static_cast<float>(i)*3.7f);
            column(x,-.2f,z,.84f,.60f,h,6,basalt,Stone,.25f*std::cos(a),.25f*std::sin(a));
            column(x,h-.2f,z,.66f,0,2.2f,5,iron,Iron,-.8f*std::sin(a),-.8f*std::cos(a));
            for(float height:{.5f,2.2f})column(x,height,z,.87f,.87f,.16f,6,brass,Brass);
        }
        // Distant, irregular spires create parallax and a real horizon in 360 degrees.
        for(int i=0;i<44;++i) {
            const float f=static_cast<float>(i),a=6.2831853f*f/44;
            const float radius=19.f+5.f*std::sin(f*2.38f);
            const float height=7.f+7.f*(.5f+.5f*std::sin(f*7.7f)),width=2.4f+.9f*std::sin(f);
            float x=radius*std::sin(a),z=radius*std::cos(a),y=-3,r=width;
            const float levels[]{0,.38f,.72f,1};
            const float widths[]{1,.8f,.35f,0};
            for(unsigned section=0;section<3;++section) {
                const float leanX=.7f*std::sin(f*3+static_cast<float>(section)),leanZ=.65f*std::cos(f*4+static_cast<float>(section));
                const float rise=height*(levels[section+1]-levels[section]),next=width*widths[section+1];
                column(x,y,z,r,next,rise,7,{.12f,.155f,.19f},Stone,leanX,leanZ);
                x+=leanX;y+=rise;z+=leanZ;r=next;
            }
            // Loose angular outcrops interrupt the regular spacing of the peaks.
            column(x+width,y*.15f,z-width*.6f,width*.73f,0,height*.36f,5,basalt,Stone,.8f,-.6f);
        }
        // Inside-facing sky shell; the shader paints a fixed dusk sky, not a flat clear.
        for(int y=0;y<12;++y)for(int x=0;x<48;++x) {
            const auto sphere=[](int px,int py) {
                const float a=6.2831853f*static_cast<float>(px)/48,b=3.14159265f*static_cast<float>(py)/12;
                return XMFLOAT3{60.f*std::sin(b)*std::sin(a),60.f*std::cos(b),60.f*std::sin(b)*std::cos(a)};
            };
            // Pole triangles avoid degenerate normals.
            if(y)triangle(sphere(x,y),sphere(x+1,y),sphere(x+1,y+1),{1,1,1},Sky);
            if(y<11)triangle(sphere(x,y),sphere(x+1,y+1),sphere(x,y+1),{1,1,1},Sky);
        }
    }
};
}
