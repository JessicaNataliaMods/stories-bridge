#include "lcs/ui_menu.hpp"
#include <algorithm>
#include <string>
#include <cmath>
namespace lcs::ui {
bool menu_text_supported(std::string_view text){
    for(unsigned char c:text)if(c>=0x80 || (c<0x20 && c!='\n' && c!='\r'))return false;
    for(size_t p=0;(p=text.find('~',p))!=std::string_view::npos;){
        if(p+2>=text.size() || text[p+2]!='~' || std::string_view("BYbghlopqrtwxyn").find(text[p+1])==std::string_view::npos)return false;
        p+=3;
    }
    return true;
}
namespace {
std::vector<std::string> paragraphs(std::string_view text){
    std::vector<std::string> result(1);
    for(size_t i=0;i<text.size();){
        if(text[i]=='\n'){result.emplace_back();++i;}
        else if(text.substr(i,3)=="~n~"){result.emplace_back();i+=3;}
        else if(text[i]=='\r')++i;
        else result.back()+=text[i++];
    }
    return result;
}
TextStyle style(const MenuFont& font){
    auto s=font.text;s.scale_y=font.line_height/18.f;
    if(font.outline)s.drop_shadow=0;
    return s;
}
}
float menu_text_width(const MenuFont& font,std::string_view text,bool spaces){
    const auto s=style(font);float width=0;
    for(const auto& paragraph:paragraphs(text))width=std::max(width,string_width(s,paragraph,spaces));
    return width;
}
MenuText layout_menu_text(const MenuFont& font,float x,float y,std::string_view text,bool from_bottom){
    MenuText result;
    if(!std::isfinite(font.line_height) || font.line_height<=0.f || font.line_height>2048.f)return result;
    const auto s=style(font);const auto parts=paragraphs(text);
    for(const auto& p:parts)result.lines+=p.empty()?1:count_lines(s,x,p,font.wrap_x);
    if(from_bottom)y-=result.lines*font.line_height;
    const auto add_box=[&](const TextRect& box){
        if(!result.has_box){result.box=box;result.has_box=true;}
        else {result.box.left=std::min(result.box.left,box.left);result.box.top=std::min(result.box.top,box.top);
              result.box.right=std::max(result.box.right,box.right);result.box.bottom=std::max(result.box.bottom,box.bottom);}
    };
    for(const auto& p:parts){
        if(p.empty()){y+=font.line_height;continue;}
        if(font.background)add_box(text_rect(s,x,y,p,font.wrap_x));
        print_block(s,x,y,p,font.wrap_x,result.quads);
        y+=count_lines(s,x,p,font.wrap_x)*font.line_height;
    }
    result.box_colour=font.background_colour;
    if(font.outline>0 && !result.quads.empty()){
        const auto foreground=std::move(result.quads);const float d=float(std::min(font.outline,4));
        result.quads.reserve(foreground.size()*9);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            if(dx==0&&dy==0)continue;
            for(auto q:foreground){for(size_t i=0;i<4;++i){q.x[i]+=dx*d;q.y[i]+=dy*d;}q.colour=s.drop_colour;result.quads.push_back(q);}
        }
        result.quads.insert(result.quads.end(),foreground.begin(),foreground.end());
    }
    return result;
}
}
