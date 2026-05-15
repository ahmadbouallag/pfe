#include "pdf_content_stream.h"
#include "pdf_xref.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace Pdf {

// ============================================================
//  CollectedPath::computeBounds
// ============================================================

void CollectedPath::computeBounds() {
    if (segments.empty()) return;
    double minX=1e18,minY=1e18,maxX=-1e18,maxY=-1e18;
    auto expand=[&](double x,double y){
        minX=std::min(minX,x);minY=std::min(minY,y);
        maxX=std::max(maxX,x);maxY=std::max(maxY,y);
    };
    for(auto& s:segments){
        expand(s.x1,s.y1);
        if(s.op==PathSegment::Op::CubicTo){expand(s.cx1,s.cy1);expand(s.cx2,s.cy2);}
    }
    x=minX;y=minY;w=maxX-minX;h=maxY-minY;
}

// ============================================================
//  Constructor
// ============================================================

ContentStreamInterpreter::ContentStreamInterpreter(
        PdfXref& xref, PdfFontLoader& fl, const PdfDict* res, double pH)
    : m_xref(xref), m_fontLoader(fl), m_resources(res), m_pageH(pH)
{ m_gsStack.push(m_gs); }

// ============================================================
//  Main entry
// ============================================================

void ContentStreamInterpreter::process(const std::vector<uint8_t>& data) {
    auto tokens = tokenise(data);
    auto it = tokens.begin();
    while (it != tokens.end()) {
        if (it->kind == Token::Kind::Keyword) {
            if (it->raw == "BI") {
                ++it;
                processInlineImage(it, tokens.end());
                continue;
            }
            execOperator(it->raw);
            m_operandStack.clear();
        } else {
            m_operandStack.push_back(*it);
        }
        ++it;
    }
}

// ============================================================
//  Tokeniser
// ============================================================

static bool isWS(uint8_t c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool isDelim(uint8_t c){
    return c=='('||c==')'||c=='<'||c=='>'||c=='['||c==']'||
           c=='{'||c=='}'||c=='/'||c=='%';
}
static int hexV(uint8_t c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

std::vector<ContentStreamInterpreter::Token>
ContentStreamInterpreter::tokenise(const std::vector<uint8_t>& data) {
    std::vector<Token> tokens;
    size_t pos=0, sz=data.size();

    auto skipWS=[&]{while(pos<sz&&isWS(data[pos]))++pos;};
    auto skipComment=[&]{while(pos<sz&&data[pos]!='\n'&&data[pos]!='\r')++pos;};

    while(pos<sz){
        skipWS(); if(pos>=sz)break;
        uint8_t c=data[pos];
        if(c=='%'){skipComment();continue;}

        if(c=='['){++pos;Token t;t.kind=Token::Kind::ArrayBegin;t.raw="[";tokens.push_back(t);continue;}
        if(c==']'){++pos;Token t;t.kind=Token::Kind::ArrayEnd;t.raw="]";tokens.push_back(t);continue;}
        if(c=='<'&&pos+1<sz&&data[pos+1]=='<'){pos+=2;Token t;t.kind=Token::Kind::DictBegin;t.raw="<<";tokens.push_back(t);continue;}
        if(c=='>'&&pos+1<sz&&data[pos+1]=='>'){pos+=2;Token t;t.kind=Token::Kind::DictEnd;t.raw=">>";tokens.push_back(t);continue;}

        if(c=='/'){
            ++pos; std::string name;
            while(pos<sz&&!isWS(data[pos])&&!isDelim(data[pos])){
                if(data[pos]=='#'&&pos+2<sz){
                    int hi=hexV(data[pos+1]),lo=hexV(data[pos+2]);
                    if(hi>=0&&lo>=0){name+=(char)((hi<<4)|lo);pos+=3;continue;}
                }
                name+=(char)data[pos++];
            }
            Token t;t.kind=Token::Kind::Name;t.raw=name;tokens.push_back(t);continue;
        }

        if(c=='('){
            ++pos; std::string s; int depth=1;
            while(pos<sz&&depth>0){
                uint8_t ch=data[pos++];
                if(ch=='\\'){
                    if(pos>=sz)break;
                    uint8_t esc=data[pos++];
                    switch(esc){
                    case 'n':s+='\n';break; case 'r':s+='\r';break; case 't':s+='\t';break;
                    case 'b':s+='\b';break; case 'f':s+='\f';break; case '(':s+='(';break;
                    case ')':s+=')';break;  case '\\':s+='\\';break;
                    case '\r':if(pos<sz&&data[pos]=='\n')++pos;break; case '\n':break;
                    default:
                        if(esc>='0'&&esc<='7'){int oct=esc-'0';
                            for(int i=0;i<2&&pos<sz&&data[pos]>='0'&&data[pos]<='7';++i)
                                oct=oct*8+(data[pos++]-'0');
                            s+=(char)(oct&0xFF);
                        }else s+=(char)esc;
                    }
                }else if(ch=='('){depth++;s+=ch;}
                else if(ch==')'){if(--depth>0)s+=ch;}
                else s+=ch;
            }
            Token t;t.kind=Token::Kind::String;t.raw=s;tokens.push_back(t);continue;
        }

        if(c=='<'){
            ++pos; std::string hex;
            while(pos<sz&&data[pos]!='>'){if(!isWS(data[pos]))hex+=(char)data[pos];++pos;}
            if(pos<sz)++pos;
            std::string s;
            for(size_t i=0;i+1<hex.size();i+=2){int hi=hexV(hex[i]),lo=hexV(hex[i+1]);if(hi>=0&&lo>=0)s+=(char)((hi<<4)|lo);}
            if(hex.size()%2==1){int hi=hexV(hex.back());if(hi>=0)s+=(char)(hi<<4);}
            Token t;t.kind=Token::Kind::String;t.raw=s;tokens.push_back(t);continue;
        }

        if((c>='0'&&c<='9')||c=='-'||c=='+'||(c=='.'&&pos+1<sz&&data[pos+1]>='0'&&data[pos+1]<='9')){
            std::string raw; bool isF=false;
            if(c=='-'||c=='+'){raw+=(char)c;++pos;}
            while(pos<sz){uint8_t ch=data[pos];if(ch>='0'&&ch<='9'){raw+=(char)ch;++pos;}else if(ch=='.'&&!isF){raw+='.';isF=true;++pos;}else break;}
            Token t; t.raw=raw;
            if(isF){t.kind=Token::Kind::Real;try{t.numVal=std::stod(raw);}catch(...){}}
            else{t.kind=Token::Kind::Integer;try{t.numVal=(double)std::stoll(raw);}catch(...){}}
            tokens.push_back(t);continue;
        }

        {std::string kw;while(pos<sz&&!isWS(data[pos])&&!isDelim(data[pos]))kw+=(char)data[pos++];
         if(!kw.empty()){Token t;t.kind=Token::Kind::Keyword;t.raw=kw;tokens.push_back(t);}}
    }
    return tokens;
}

ContentStreamInterpreter::Token ContentStreamInterpreter::makeToken(const std::string& s){
    Token t; t.kind=Token::Kind::Keyword; t.raw=s; return t;
}

// ============================================================
//  Operator dispatcher
// ============================================================

void ContentStreamInterpreter::execOperator(const std::string& op) {
    auto nums=[&](int n){return popNumbers(n);};

    if(op=="q"){op_q();return;} if(op=="Q"){op_Q();return;}
    if(op=="cm"){auto v=nums(6);op_cm(v[0],v[1],v[2],v[3],v[4],v[5]);return;}
    if(op=="BT"){op_BT();return;} if(op=="ET"){op_ET();return;}
    if(op=="Tf"){auto n=popName();auto v=nums(1);op_Tf(n,v[0]);return;}
    if(op=="Tm"){auto v=nums(6);op_Tm(v[0],v[1],v[2],v[3],v[4],v[5]);return;}
    if(op=="Td"){auto v=nums(2);op_Td(v[0],v[1]);return;}
    if(op=="TD"){auto v=nums(2);op_TD(v[0],v[1]);return;}
    if(op=="T*"){op_Tstar();return;}
    if(op=="Tc"){auto v=nums(1);op_Tc(v[0]);return;}
    if(op=="Tw"){auto v=nums(1);op_Tw(v[0]);return;}
    if(op=="Tz"){auto v=nums(1);op_Tz(v[0]);return;}
    if(op=="TL"){auto v=nums(1);op_TL(v[0]);return;}
    if(op=="Ts"){auto v=nums(1);op_Ts(v[0]);return;}
    if(op=="Tr"){nums(1);return;}

    if(op=="Tj"||op=="'"||op=="\""){
        if(op=="'") op_Tstar();
        if(op=="\""){auto v=nums(2);op_Tw(v[0]);op_Tc(v[1]);op_Tstar();}
        op_Tj(popString()); return;
    }
    if(op=="TJ"){op_TJ(m_operandStack);return;}

    if(op=="m"){auto v=nums(2);op_m(v[0],v[1]);return;}
    if(op=="l"){auto v=nums(2);op_l(v[0],v[1]);return;}
    if(op=="c"){auto v=nums(6);op_c(v[0],v[1],v[2],v[3],v[4],v[5]);return;}
    if(op=="v"){auto v=nums(4);op_v(v[0],v[1],v[2],v[3]);return;}
    if(op=="y"){auto v=nums(4);op_y(v[0],v[1],v[2],v[3]);return;}
    if(op=="re"){auto v=nums(4);op_re(v[0],v[1],v[2],v[3]);return;}
    if(op=="h"){op_h();return;}
    if(op=="S"){op_S(true,false);return;}   if(op=="s"){op_S(true,false,false,true);return;}
    if(op=="f"||op=="F"){op_S(false,true);return;} if(op=="f*"){op_S(false,true,true);return;}
    if(op=="B"){op_S(true,true);return;}    if(op=="B*"){op_S(true,true,true);return;}
    if(op=="b"){op_S(true,true,false,true);return;} if(op=="b*"){op_S(true,true,true,true);return;}
    if(op=="n"){op_n();return;}

    if(op=="g"){auto v=nums(1);op_setGray(v[0],false);return;}
    if(op=="G"){auto v=nums(1);op_setGray(v[0],true);return;}
    if(op=="rg"){auto v=nums(3);op_setRGB(v[0],v[1],v[2],false);return;}
    if(op=="RG"){auto v=nums(3);op_setRGB(v[0],v[1],v[2],true);return;}
    if(op=="k"){auto v=nums(4);op_setCMYK(v[0],v[1],v[2],v[3],false);return;}
    if(op=="K"){auto v=nums(4);op_setCMYK(v[0],v[1],v[2],v[3],true);return;}
    if(op=="sc"||op=="SC"||op=="scn"||op=="SCN"||op=="cs"||op=="CS"){m_operandStack.clear();return;}

    if(op=="Do"){auto n=popName();op_Do(n);return;}
    if(op=="w"){nums(1);return;}
    m_operandStack.clear();
}

// ============================================================
//  Text operators
// ============================================================

void ContentStreamInterpreter::op_BT(){m_inText=true;m_gs.text.textMatrix=m_gs.text.lineMatrix=Matrix2D::identity();}
void ContentStreamInterpreter::op_ET(){m_inText=false;}

void ContentStreamInterpreter::op_Tf(const std::string& name,double size){
    m_gs.text.fontResName=name; m_gs.text.fontSize=size; m_gs.text.font=resolveFont(name);
}
void ContentStreamInterpreter::op_Tm(double a,double b,double c,double d,double e,double f){
    m_gs.text.textMatrix=m_gs.text.lineMatrix=Matrix2D(a,b,c,d,e,f);
}
void ContentStreamInterpreter::op_Td(double tx,double ty){
    m_gs.text.lineMatrix=Matrix2D(1,0,0,1,tx,ty)*m_gs.text.lineMatrix;
    m_gs.text.textMatrix=m_gs.text.lineMatrix;
}
void ContentStreamInterpreter::op_TD(double tx,double ty){op_TL(-ty);op_Td(tx,ty);}
void ContentStreamInterpreter::op_Tstar(){op_Td(0,-m_gs.text.leading);}
void ContentStreamInterpreter::op_Tc(double c){m_gs.text.charSpacing=c;}
void ContentStreamInterpreter::op_Tw(double w){m_gs.text.wordSpacing=w;}
void ContentStreamInterpreter::op_Tz(double h){m_gs.text.hScale=h/100.0;}
void ContentStreamInterpreter::op_TL(double l){m_gs.text.leading=l;}
void ContentStreamInterpreter::op_Ts(double r){m_gs.text.rise=r;}

void ContentStreamInterpreter::op_Tj(const std::string& pdfStr) {
    if(!m_inText) return;
    PdfFont* font=m_gs.text.font;
    bool twoByteChars=false;
    if(font){for(auto& kv:font->toUnicode){if(kv.first>0xFF){twoByteChars=true;break;}}}

    QString text;
    double advanceTotal=0.0;
    double scale=m_gs.text.fontSize*m_gs.text.hScale;

    if(font){
        text=font->decode(pdfStr,twoByteChars);
        if(twoByteChars){
            for(size_t i=0;i+1<pdfStr.size();i+=2){
                uint32_t code=((uint8_t)pdfStr[i]<<8)|(uint8_t)pdfStr[i+1];
                double w=font->charWidth(code)*font->fontMatrixScale*scale;
                if(pdfStr[i]==0&&pdfStr[i+1]==0x20)w+=m_gs.text.wordSpacing;
                advanceTotal+=w+m_gs.text.charSpacing;
            }
        }else{
            for(unsigned char ch:pdfStr){
                double w=font->charWidth(ch)*font->fontMatrixScale*scale;
                if(ch==0x20)w+=m_gs.text.wordSpacing;
                advanceTotal+=w+m_gs.text.charSpacing;
            }
        }
    }else{
        for(unsigned char ch:pdfStr) text+=QChar(ch);
        advanceTotal=text.size()*m_gs.text.fontSize*0.6;
    }

    Matrix2D combined=m_gs.text.textMatrix*m_gs.ctm;
    auto[px,py]=combined.map(0,m_gs.text.rise);
    if(!text.trimmed().isEmpty()) emitTextSpan(text,px,flipY(py),advanceTotal);

    m_gs.text.textMatrix=Matrix2D(1,0,0,1,advanceTotal,0)*m_gs.text.textMatrix;
}

void ContentStreamInterpreter::op_TJ(const std::vector<Token>& stack) {
    if(!m_inText) return;
    bool inArr=false;
    for(const auto& tok:stack){
        if(tok.kind==Token::Kind::ArrayBegin){inArr=true;continue;}
        if(tok.kind==Token::Kind::ArrayEnd){inArr=false;continue;}
        if(!inArr) continue;
        if(tok.kind==Token::Kind::String) op_Tj(tok.raw);
        else if(tok.kind==Token::Kind::Integer||tok.kind==Token::Kind::Real){
            double disp=-tok.numVal/1000.0*m_gs.text.fontSize*m_gs.text.hScale;
            m_gs.text.textMatrix=Matrix2D(1,0,0,1,disp,0)*m_gs.text.textMatrix;
        }
    }
}

// ============================================================
//  Path operators
// ============================================================

void ContentStreamInterpreter::op_m(double x,double y){
    auto[px,py]=m_gs.ctm.map(x,y);m_currentX=px;m_currentY=py;
    m_currentPath.push_back({PathSegment::Op::MoveTo,px,py});
}
void ContentStreamInterpreter::op_l(double x,double y){
    auto[px,py]=m_gs.ctm.map(x,y);
    m_currentPath.push_back({PathSegment::Op::LineTo,px,py});
    m_currentX=px;m_currentY=py;
}
void ContentStreamInterpreter::op_c(double x1,double y1,double x2,double y2,double x3,double y3){
    auto[px1,py1]=m_gs.ctm.map(x1,y1);auto[px2,py2]=m_gs.ctm.map(x2,y2);auto[px3,py3]=m_gs.ctm.map(x3,y3);
    PathSegment s;s.op=PathSegment::Op::CubicTo;s.cx1=px1;s.cy1=py1;s.cx2=px2;s.cy2=py2;s.x1=px3;s.y1=py3;
    m_currentPath.push_back(s);m_currentX=px3;m_currentY=py3;
}
void ContentStreamInterpreter::op_v(double x2,double y2,double x3,double y3){op_c(m_currentX,m_currentY,x2,y2,x3,y3);}
void ContentStreamInterpreter::op_y(double x1,double y1,double x3,double y3){op_c(x1,y1,x3,y3,x3,y3);}
void ContentStreamInterpreter::op_re(double x,double y,double w,double h){op_m(x,y);op_l(x+w,y);op_l(x+w,y+h);op_l(x,y+h);op_h();}
void ContentStreamInterpreter::op_h(){m_currentPath.push_back({PathSegment::Op::Close,m_currentX,m_currentY});}

void ContentStreamInterpreter::op_S(bool stroke,bool fill,bool /*evenOdd*/,bool close){
    if(close&&!m_currentPath.empty())op_h();
    if(!m_currentPath.empty()){
        CollectedPath cp;
        cp.segments=m_currentPath;cp.filled=fill;cp.stroked=stroke;
        cp.fillR=m_gs.fillR;cp.fillG=m_gs.fillG;cp.fillB=m_gs.fillB;cp.fillA=m_gs.fillA;
        cp.strokeR=m_gs.strokeR;cp.strokeG=m_gs.strokeG;cp.strokeB=m_gs.strokeB;cp.strokeA=m_gs.strokeA;
        cp.lineWidth=m_gs.lineWidth;
        for(auto& seg:cp.segments){seg.y1=flipY(seg.y1);seg.cy1=flipY(seg.cy1);seg.cy2=flipY(seg.cy2);}
        cp.computeBounds();
        m_content.paths.push_back(cp);
    }
    m_currentPath.clear();
}
void ContentStreamInterpreter::op_n(){m_currentPath.clear();}

// ============================================================
//  Color operators
// ============================================================

void ContentStreamInterpreter::op_setGray(double g,bool stroke){
    if(stroke){m_gs.strokeR=m_gs.strokeG=m_gs.strokeB=g;}else{m_gs.fillR=m_gs.fillG=m_gs.fillB=g;}
}
void ContentStreamInterpreter::op_setRGB(double r,double g,double b,bool stroke){
    if(stroke){m_gs.strokeR=r;m_gs.strokeG=g;m_gs.strokeB=b;}else{m_gs.fillR=r;m_gs.fillG=g;m_gs.fillB=b;}
}
void ContentStreamInterpreter::op_setCMYK(double c,double m,double y,double k,bool stroke){
    op_setRGB(std::max(0.0,1.0-c-k),std::max(0.0,1.0-m-k),std::max(0.0,1.0-y-k),stroke);
}

// ============================================================
//  Graphics state
// ============================================================

void ContentStreamInterpreter::op_q(){m_gsStack.push(m_gs);}
void ContentStreamInterpreter::op_Q(){if(!m_gsStack.empty()){m_gs=m_gsStack.top();m_gsStack.pop();}}
void ContentStreamInterpreter::op_cm(double a,double b,double c,double d,double e,double f){
    m_gs.ctm=Matrix2D(a,b,c,d,e,f)*m_gs.ctm;
}

// ============================================================
//  XObjects
// ============================================================

void ContentStreamInterpreter::op_Do(const std::string& name){
    if(!m_resources)return;
    const PdfObject* xobjDict=m_resources->get("XObject");
    if(!xobjDict)return;
    PdfObject xobjRes=m_xref.deref(*xobjDict);
    if(!xobjRes.isDict())return;
    const PdfObject* xobj=xobjRes.asDict()->get(name);
    if(!xobj)return;
    PdfObject xobjResolved=m_xref.deref(*xobj);
    if(!xobjResolved.isStream())return;
    PdfStream* stm=xobjResolved.asStream();
    const PdfObject* subtypeObj=stm->dict.get("Subtype");
    if(!subtypeObj||!subtypeObj->isName())return;
    if(subtypeObj->asName()=="Image"){processImageXObject(stm);}
    else if(subtypeObj->asName()=="Form"){
        const PdfDict* formRes=m_resources;
        PdfObject formResResolved;
        const PdfObject* frObj=stm->dict.get("Resources");
        if(frObj){formResResolved=m_xref.deref(*frObj);if(formResResolved.isDict())formRes=formResResolved.asDict();}
        const PdfObject* matObj=stm->dict.get("Matrix");
        bool hasMat=matObj&&matObj->isArray()&&matObj->asArray()->size()>=6;
        if(hasMat){PdfArray* ma=matObj->asArray();op_q();
            op_cm(ma->at(0).numOr(1),ma->at(1).numOr(0),ma->at(2).numOr(0),ma->at(3).numOr(1),ma->at(4).numOr(0),ma->at(5).numOr(0));}
        ContentStreamInterpreter sub(m_xref,m_fontLoader,formRes,m_pageH);
        sub.m_gs=m_gs;sub.process(stm->data);
        for(auto& ts:sub.m_content.textSpans)m_content.textSpans.push_back(ts);
        for(auto& p:sub.m_content.paths)m_content.paths.push_back(p);
        for(auto& im:sub.m_content.images)m_content.images.push_back(im);
        if(hasMat)op_Q();
    }
}

void ContentStreamInterpreter::processImageXObject(PdfStream* stm){
    ImageRef img;
    auto getI=[&](const char* k)->int{const PdfObject* o=stm->dict.get(k);return o?(int)m_xref.deref(*o).intOr(0):0;};
    img.imageWidth=getI("Width"); img.imageHeight=getI("Height");
    img.bitsPerComponent=getI("BitsPerComponent");if(!img.bitsPerComponent)img.bitsPerComponent=8;
    const PdfObject* csObj=stm->dict.get("ColorSpace");
    if(csObj){PdfObject cs=m_xref.deref(*csObj);
        if(cs.isName())img.colorSpace=cs.asName();
        else if(cs.isArray()&&cs.asArray()->size()>0&&cs.asArray()->at(0).isName())img.colorSpace=cs.asArray()->at(0).asName();}
    const PdfObject* fObj=stm->dict.get("Filter");
    if(fObj){PdfObject f=m_xref.deref(*fObj);std::string fn;
        if(f.isName())fn=f.asName();else if(f.isArray()&&f.asArray()->size()>0)fn=f.asArray()->at(0).asName();
        img.isJpeg=(fn=="DCTDecode"||fn=="DCT");}
    img.data=stm->data;
    auto[x0,y0]=m_gs.ctm.map(0,0);auto[x1,y1]=m_gs.ctm.map(1,1);
    img.x=std::min(x0,x1);img.y=flipY(std::max(y0,y1));img.w=std::abs(x1-x0);img.h=std::abs(y1-y0);
    m_content.images.push_back(img);
}

void ContentStreamInterpreter::processInlineImage(
        std::vector<Token>::iterator& it,
        std::vector<Token>::iterator end) {
    ImageRef img;
    while(it!=end&&!(it->kind==Token::Kind::Keyword&&it->raw=="ID")){
        if(it->kind==Token::Kind::Name){
            std::string key=it->raw;++it;if(it==end)break;
            if((key=="W"||key=="Width")&&(it->kind==Token::Kind::Integer||it->kind==Token::Kind::Real))img.imageWidth=(int)it->numVal;
            if((key=="H"||key=="Height")&&(it->kind==Token::Kind::Integer||it->kind==Token::Kind::Real))img.imageHeight=(int)it->numVal;
            if((key=="F"||key=="Filter")&&it->kind==Token::Kind::Name)img.isJpeg=(it->raw=="DCTDecode"||it->raw=="DCT");
        }
        ++it;
    }
    if(it!=end)++it; // skip ID
    while(it!=end&&!(it->kind==Token::Kind::Keyword&&it->raw=="EI"))++it;
    if(it!=end)++it;
    auto[x0,y0]=m_gs.ctm.map(0,0);auto[x1,y1]=m_gs.ctm.map(1,1);
    img.x=std::min(x0,x1);img.y=flipY(std::max(y0,y1));img.w=std::abs(x1-x0);img.h=std::abs(y1-y0);
    m_content.images.push_back(img);
}

// ============================================================
//  Helpers
// ============================================================

PdfFont* ContentStreamInterpreter::resolveFont(const std::string& resName){
    if(!m_resources)return nullptr;
    const PdfObject* fontDict=m_resources->get("Font");if(!fontDict)return nullptr;
    PdfObject fd=m_xref.deref(*fontDict);if(!fd.isDict())return nullptr;
    const PdfObject* fontObj=fd.asDict()->get(resName);if(!fontObj)return nullptr;
    return m_fontLoader.loadFont(*fontObj,m_xref);
}

void ContentStreamInterpreter::emitTextSpan(const QString& text,double x,double y,double width){
    if(text.trimmed().isEmpty())return;
    TextSpan ts;
    ts.x=x;ts.y=y;ts.width=width;ts.fontSize=m_gs.text.fontSize;
    ts.r=m_gs.fillR;ts.g=m_gs.fillG;ts.b=m_gs.fillB;
    if(m_gs.text.font){
        ts.fontFamily=m_gs.text.font->qtFamilyName; // std::string = std::string ✓
        ts.bold=m_gs.text.font->bold;ts.italic=m_gs.text.font->italic;
    }
    ts.text=text;ts.leading=m_gs.text.leading;
    m_content.textSpans.push_back(ts);
}

std::vector<double> ContentStreamInterpreter::popNumbers(int count){
    std::vector<double> nums;nums.reserve(count);
    int start=(int)m_operandStack.size()-count;if(start<0)start=0;
    for(int i=start;i<(int)m_operandStack.size();++i){
        const Token& t=m_operandStack[i];
        nums.push_back((t.kind==Token::Kind::Integer||t.kind==Token::Kind::Real)?t.numVal:0.0);
    }
    while((int)nums.size()<count)nums.insert(nums.begin(),0.0);
    return nums;
}

std::string ContentStreamInterpreter::popString(){
    for(int i=(int)m_operandStack.size()-1;i>=0;--i)
        if(m_operandStack[i].kind==Token::Kind::String)return m_operandStack[i].raw;
    return {};
}

std::string ContentStreamInterpreter::popName(){
    for(int i=(int)m_operandStack.size()-1;i>=0;--i)
        if(m_operandStack[i].kind==Token::Kind::Name)return m_operandStack[i].raw;
    return {};
}

} // namespace Pdf
