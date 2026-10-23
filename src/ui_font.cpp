#include "lcs/ui_font.hpp"
#include <algorithm>
#include <cmath>
namespace lcs::ui {
namespace {
const int16_t size_table[font_style_count][419]={
#include "ui_font_tables.inc"
};
constexpr int size_increment[font_style_count]={4,-2,2};
constexpr int fixed_width_slot=418,extra_offset=209,glyph_count=209;
bool punctuation(int c){
    switch(c){case '!':case '?':case '.':case ',':case ':':case ';':return true;}
    return false;
}
int style_index(const TextStyle& s){return int(s.style);}
bool uses_half_texture(const TextStyle& s){
    // reStories CFont::SetFontStyle enables the reduced glyph atlas for heading
    // unconditionally. Keep that rule even when callers only set the style.
    return s.half_texture || s.style==FontStyle::heading;
}
// Colour tokens of the original ParseToken. Only g/h/l/w replace the base colour
// for the remainder of the string; the rest recolour the current run.
struct Token {size_t next=0;bool bold_toggle=false;bool has_colour=false;Rgba colour;};
Token parse_token(const TextStyle& style,std::string_view s,size_t at){
    Token t;
    size_t i=at+1;
    const char code=i<s.size()?s[i]:'\0';
    if(style.colour.r || style.colour.g || style.colour.b) {
        const auto set=[&](uint8_t r,uint8_t g,uint8_t b){t.has_colour=true;t.colour.r=r;t.colour.g=g;t.colour.b=b;};
        switch(code){
        case 'B':t.bold_toggle=true;break;
        case 'Y':set(255,227,79);break;
        case 'b':set(77,155,210);break;
        case 'g':set(75,151,75);break;
        case 'h':set(255,255,255);break;
        case 'l':set(0,0,0);break;
        case 'o':set(229,125,126);break;
        case 'p':set(151,82,197);break;
        case 'q':set(199,144,203);break;
        case 'r':set(174,0,0);break;
        case 't':set(86,212,146);break;
        case 'w':set(225,225,225);break;
        case 'x':set(132,146,197);break;
        case 'y':set(255,255,0);break;
        default:break;
        }
    }
    while(i<s.size() && s[i]!='~')++i;
    t.next=i<s.size()?i+1:s.size();
    return t;
}
void add_glyph(const TextStyle& st,float x,float y,int c,Rgba colour,bool right_adjust,std::vector<GlyphQuad>& out){
    if(c<0 || c>=glyph_count)return;
    const bool blank=c==0;
    const float w=glyph_advance(st,c)/32.f;
    if(!st.proportional && right_adjust)x+=(glyph_advance(st,c)-glyph_advance(st,c,true))*st.scale_x;
    if(uses_half_texture(st) && c==208)c=0;
    const float xoff=float(c%16),yoff=float(c/16);
    GlyphQuad q;q.colour=colour;q.page=style_index(st);
    const auto uv=[&](float u0,float v0,float u1,float v1,float u2,float v2,float u3,float v3){
        q.u={u0,u1,u2,u3};q.v={v0,v1,v2,v3};
    };
    const auto rect=[&](float l,float t,float r,float b){q.x={l,r,l,r};q.y={t,t,b,b};};
    if(st.style==FontStyle::bank || st.style==FontStyle::standard){
        if(blank)return;
        if(st.slant==0.f){
            if(c<192){
                rect(x,y,x+32.f*st.scale_x*0.96f,y+40.f*st.scale_y*0.5f);
                uv(xoff/16.f,yoff/12.8f+0.0021f,(xoff+1.f)/16.f-0.001f,yoff/12.8f+0.0021f,
                   xoff/16.f,(yoff+1.f)/12.8f-0.0021f,(xoff+1.f)/16.f-0.001f,(yoff+1.f)/12.8f-0.0021f);
            }else{
                const float xscale=0.54f;
                rect(x,y+st.scale_y*4.f,x+32.f*st.scale_y*xscale*0.96f,y+4.f*st.scale_y+16.f*st.scale_y);
                uv(xoff/16.f,yoff/12.8f+0.0021f,(xoff+1.f)/16.f-0.001f,yoff/12.8f+0.0021f,
                   xoff/16.f,(yoff+1.f)/12.8f-0.017f,(xoff+1.f)/16.f-0.001f,(yoff+1.f)/12.8f-0.017f);
            }
        }else{
            rect(x,y,x+32.f*st.scale_x*0.96f,y+40.f*st.scale_y*0.5f);
            uv(xoff/16.f,yoff/12.8f+0.00055f,(xoff+1.f)/16.f-0.001f,yoff/12.8f+0.0021f+0.01f,
               xoff/16.f,(yoff+1.f)/12.8f-0.009f,(xoff+1.f)/16.f-0.001f,(yoff+1.f)/12.8f-0.0021f+0.01f);
        }
    }else{
        if(blank)return;
        rect(x,y,x+32.f*st.scale_x*w,y+32.f*st.scale_y*0.5f);
        uv(xoff/16.f,yoff/6.4f,(xoff+w)/16.f,yoff/6.4f,xoff/16.f,(yoff+1.f)/6.4f,(xoff+w)/16.f,(yoff+1.f)/6.4f);
    }
    out.push_back(q);
}
void emit(const TextStyle& st,float x,float y,std::string_view text,Rgba base,bool shadow,std::vector<GlyphQuad>& out){
    Rgba colour=base;bool bold=false;
    TextStyle local=st;
    for(size_t i=0;i<text.size();){
        if(text[i]=='~'){
            const auto token=parse_token(local,text,i);
            if(token.bold_toggle)bold=!bold;
            if(token.has_colour && !shadow){
                // Only the opaque white/green/black/grey codes change the base colour
                // for the rest of the string; others affect this run alike.
                colour.r=token.colour.r;colour.g=token.colour.g;colour.b=token.colour.b;
            }
            i=token.next;
            continue;
        }
        int c=int(uint8_t(text[i]))-' ';
        ++i;
        if(c==200)c='^';
        if(c==201)c='>';
        if(uses_half_texture(st))c=new_character(c);
        float gy=y;
        if(st.slant!=0.f)gy=(st.slant_ref_x-x)*st.slant+st.slant_ref_y;
        add_glyph(st,x,gy,c,colour,st.right_justify,out);
        if(bold){
            add_glyph(st,x+1.f,gy,c,colour,st.right_justify,out);
            add_glyph(st,x+2.f,gy,c,colour,st.right_justify,out);
            x+=2.f;
        }
        x+=st.scale_x*glyph_advance(st,c);
    }
}
}
float glyph_advance(const TextStyle& s,int c,bool force_proportional){
    const int st=style_index(s);
    if(c<0 || c>=glyph_count)return 0.f;
    if(s.proportional || force_proportional){
        const float inc=c<192?0.f:4.f;
        return float(size_table[st][c]+size_table[st][c+extra_offset]+size_increment[st])+inc;
    }
    if(punctuation(c+' '))return float(size_table[st][fixed_width_slot])/1.6f+float(size_increment[st]);
    return float(size_table[st][fixed_width_slot]+size_increment[st]);
}
int new_character(int c){
    if(c>=33 && c<=33+25)return c-6;
    if(c>=65 && c<=65+25)return c-38;
    if(c>=96 && c<=96+31)return c-43;
    if(c>=128 && c<=128+31)return c-75;
    switch(c){
    case 190:return 90;
    case 175:return 87;
    case 184:return 88;
    case 187:return 89;
    case 31:return 88;
    }
    if(c>=27 && c<31)return 2;
    if(c>=180 && c<256)return 2;
    return c;
}
float string_width(const TextStyle& s,std::string_view text,bool spaces){
    float w=0.f;
    for(size_t i=0;i<text.size();){
        const char ch=text[i];
        if(ch==' ' && !spaces)break;
        if(ch=='~'){
            ++i;
            while(i<text.size() && text[i]!='~')++i;
            if(i<text.size())++i;
            continue;
        }
        int c=int(uint8_t(ch))-' ';
        if(uses_half_texture(s))c=new_character(c);
        w+=glyph_advance(s,c)*s.scale_x;
        ++i;
    }
    return w;
}
namespace {
// Shadow first, then the text, at an already aligned left edge.
void print_at(const TextStyle& st,float left,float y,std::string_view text,std::vector<GlyphQuad>& out){
    const bool shadow_style=st.style==FontStyle::bank || st.style==FontStyle::standard;
    if(st.drop_shadow!=0 && shadow_style){
        const float d=float(st.drop_shadow);
        emit(st,left+d,y+d,text,st.drop_colour,true,out);
    }
    emit(st,left,y,text,st.colour,false,out);
}
size_t next_space(std::string_view s,size_t at){
    for(;at<s.size() && s[at]!=' ';++at)
        if(s[at]=='~'){++at;while(at<s.size() && s[at]!='~')++at;}
    return std::min(at,s.size());
}
struct Line {size_t begin,end;float width;};
// CFont::PrintString word breaking.
std::vector<Line> break_lines(const TextStyle& st,float xstart,std::string_view text,float wrap_x){
    std::vector<Line> lines;
    const float base=(st.centre || st.right_justify)?0.f:xstart;
    const float xend=st.centre?st.centre_size:st.right_justify?xstart-st.right_wrap:wrap_x;
    const float space=glyph_advance(st,0)*st.scale_x;
    size_t start=0,s=0,t=0;float x=base;bool first=true;
    for(;;){
        for(;;){
            for(;;){
                if(s>=text.size())return lines;
                if(x+string_width(st,text.substr(s),false)>xend && !first){
                    lines.push_back({start,s,x});
                    x=base;first=true;start=s;
                }else break;
            }
            t=next_space(text,s);
            if(t>=text.size() || (text[t]==' ' && t+1==text.size()))break;
            first=false;
            x+=string_width(st,text.substr(s),false)+space;
            s=t+1;
        }
        if(t<text.size() && text[t]==' ' && t+1==text.size())t=text.size();
        x+=string_width(st,text.substr(s),false);
        s=t;
        lines.push_back({start,s,x});
    }
}
float line_height(const TextStyle& st){return 32.f*st.scale_y*0.5f+2.f*st.scale_y;}
}
void print_line(const TextStyle& st,float x,float y,std::string_view text,std::vector<GlyphQuad>& out){
    if(text.empty() || text[0]=='*')return;
    float left=x;
    if(st.centre)left=x-string_width(st,text)/2.f;
    else if(st.right_justify)left=x-string_width(st,text);
    print_at(st,left,y,text,out);
}
int count_lines(const TextStyle& st,float xstart,std::string_view text,float wrap_x){
    return int(break_lines(st,xstart,text,wrap_x).size());
}
void print_block(const TextStyle& st,float x,float y,std::string_view text,float wrap_x,std::vector<GlyphQuad>& out){
    if(text.empty() || text[0]=='*')return;
    for(const auto& line:break_lines(st,x,text,wrap_x)){
        const float left=st.centre?x-line.width/2.f:st.right_justify?x-line.width:x;
        print_at(st,left,y,text.substr(line.begin,line.end-line.begin),out);
        y+=line_height(st);
    }
}
void print_block_from_bottom(const TextStyle& st,float x,float y,std::string_view text,float wrap_x,std::vector<GlyphQuad>& out){
    y-=line_height(st)*float(count_lines(st,x,text,wrap_x));
    print_block(st,x,y,text,wrap_x,out);
}
TextRect text_rect(const TextStyle& st,float xstart,float ystart,std::string_view text,float wrap_x){
    const float lines=float(count_lines(st,xstart,text,wrap_x));
    TextRect r;
    if(st.centre){
        r.left=xstart-st.centre_size*0.5f-4.f;r.right=xstart+st.centre_size*0.5f+4.f;
        r.top=ystart-2.f;r.bottom=line_height(st)*lines+ystart+2.f;
    }else{
        r.left=xstart-4.f;r.right=wrap_x;
        r.top=ystart;r.bottom=line_height(st)*lines+ystart+4.f;
    }
    return r;
}
}
