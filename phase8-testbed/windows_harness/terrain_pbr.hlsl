row_major float4x4 W:register(c0);
row_major float4x4 V:register(c4);
row_major float4x4 P:register(c8);
struct I{float3 p:POSITION0;float4 c:COLOR0;float2 a:TEXCOORD0;float2 b:TEXCOORD1;};
struct O{float4 p:POSITION0;float3 v:TEXCOORD0;float2 a:TEXCOORD1;float2 b:TEXCOORD2;float4 c:COLOR0;};
O VSMain(I i){O o;float4 w=mul(float4(i.p,1),W);float4 v=mul(w,V);o.p=mul(v,P);o.v=v.xyz;o.a=i.a;o.b=i.b;o.c=i.c;return o;}

sampler2D A0:register(s0);sampler2D A1:register(s1);sampler2D A2:register(s2);sampler2D A3:register(s3);sampler2D A4:register(s4);
sampler2D B0:register(s5);sampler2D B1:register(s6);sampler2D B2:register(s7);sampler2D B3:register(s8);sampler2D B4:register(s9);
float4 UA:register(c0);float4 UB:register(c1);float4 LP:register(c2);float4 PAR:register(c3);
struct PI{float3 v:TEXCOORD0;float2 a:TEXCOORD1;float2 b:TEXCOORD2;float4 c:COLOR0;};
float3 nn(float3 x){return normalize(x);}
float4 PSMain(PI i):COLOR0{
 float2 ua=i.a*UA.xy+UA.zw,ub=i.b*UB.xy+UB.zw;float w=saturate(i.c.a);
 float3 al=lerp(tex2D(A0,ua).rgb,tex2D(B0,ub).rgb,w);
 float3 nt=nn(lerp(tex2D(A1,ua).xyz*2-1,tex2D(B1,ub).xyz*2-1,w));
 float ao=lerp(tex2D(A2,ua).r,tex2D(B2,ub).r,w);
 float r=max(.055,saturate(lerp(tex2D(A3,ua).r,tex2D(B3,ub).r,w)));
 float sm=saturate(lerp(tex2D(A4,ua).r,tex2D(B4,ub).r,w));
 float3 dx=ddx(i.v),dy=ddy(i.v);float2 tx=ddx(ua),ty=ddy(ua);float3 vd=nn(-i.v);
 float3 ng=nn(cross(dx,dy));if(dot(ng,vd)<0)ng=-ng;
 float3 t=nn(dx*ty.y-dy*tx.y);t=nn(t-ng*dot(ng,t));float3 bb=nn(cross(ng,t));
 if(tx.x*ty.y-tx.y*ty.x<0)bb=-bb;float3 n=nn(t*nt.x+bb*nt.y+ng*nt.z);
 float3 l=nn(LP.xyz),h=nn(l+vd);float nl=saturate(dot(n,l)),nv=max(1e-4,saturate(dot(n,vd)));
 float nh=saturate(dot(n,h)),vh=saturate(dot(vd,h));float ar=r*r,a2=ar*ar,q=nh*nh*(a2-1)+1;
 float D=a2/(3.14159265*q*q+1e-5);float k=(r+1)*(r+1)*.125;
 float G=(nl/(nl*(1-k)+k))*(nv/(nv*(1-k)+k));float3 f0=lerp(.02.xxx,.16.xxx,sm);
 float fc=1-vh;fc=fc*fc*fc*fc*fc;float3 F=f0+(1-f0)*fc;
 float3 sp=D*G*F/(4*nl*nv+1e-4);float3 df=al*(1-F)*.318309886;
 float3 amb=al*i.c.rgb*lerp(.42,1,ao)*PAR.x;float lum=dot(i.c.rgb,float3(.299,.587,.114));
 float3 dir=(df+sp)*nl*LP.w*lerp(.65,1.15,saturate(lum));return float4(saturate(amb+dir),1);
}
