#ifndef PDF_CONTENT_STREAM_H
#define PDF_CONTENT_STREAM_H

// ============================================================
// pdf_content_stream.h  —  Content stream interpreter
// ============================================================

#include "pdf_object.h"
#include "pdf_font.h"
#include <QString>
#include <vector>
#include <string>
#include <stack>
#include <functional>
#include <memory>

namespace Pdf {

class PdfXref;

// ---- 2D transform matrix ----
struct Matrix2D {
    double a=1,b=0,c=0,d=1,e=0,f=0;
    Matrix2D() = default;
    Matrix2D(double a,double b,double c,double d,double e,double f)
        : a(a),b(b),c(c),d(d),e(e),f(f) {}
    Matrix2D operator*(const Matrix2D& o) const {
        return { a*o.a+b*o.c, a*o.b+b*o.d,
                 c*o.a+d*o.c, c*o.b+d*o.d,
                 e*o.a+f*o.c+o.e, e*o.b+f*o.d+o.f };
    }
    std::pair<double,double> map(double x, double y) const {
        return { a*x+c*y+e, b*x+d*y+f };
    }
    static Matrix2D identity() { return {}; }
};

// ---- Graphics state ----
struct GraphicsState {
    Matrix2D ctm;
    struct TextState {
        double   fontSize    = 12.0;
        double   charSpacing = 0.0;
        double   wordSpacing = 0.0;
        double   hScale      = 1.0;
        double   leading     = 0.0;
        double   rise        = 0.0;
        int      renderMode  = 0;
        PdfFont* font        = nullptr;
        std::string fontResName;
        Matrix2D textMatrix;
        Matrix2D lineMatrix;
    } text;
    double fillR=0,fillG=0,fillB=0,fillA=1;
    double strokeR=0,strokeG=0,strokeB=0,strokeA=1;
    double lineWidth = 1.0;
};

// ---- Text span ----
struct TextSpan {
    double x=0, y=0, width=0, fontSize=12, leading=0;
    std::string fontFamily = "Arial";   // std::string, not QString
    bool   bold=false, italic=false;
    double r=0, g=0, b=0;
    QString text;
};

// ---- Path ----
struct PathSegment {
    enum class Op { MoveTo, LineTo, CubicTo, Close } op;
    double x1=0,y1=0,cx1=0,cy1=0,cx2=0,cy2=0;
};

struct CollectedPath {
    std::vector<PathSegment> segments;
    bool   filled=false, stroked=false;
    double fillR=0,fillG=0,fillB=0,fillA=1;
    double strokeR=0,strokeG=0,strokeB=0,strokeA=1;
    double lineWidth=1;
    double x=0,y=0,w=0,h=0;
    void computeBounds();
};

// ---- Image reference ----
struct ImageRef {
    double x=0,y=0,w=0,h=0;
    std::vector<uint8_t> data;
    std::string colorSpace;
    int bitsPerComponent=8, imageWidth=0, imageHeight=0;
    bool isJpeg=false;
};

// ---- Page content ----
struct PageContent {
    std::vector<TextSpan>     textSpans;
    std::vector<CollectedPath> paths;
    std::vector<ImageRef>     images;
};

// ============================================================
//  Interpreter
// ============================================================

class ContentStreamInterpreter {
public:
    ContentStreamInterpreter(PdfXref& xref,
                             PdfFontLoader& fontLoader,
                             const PdfDict* resources,
                             double pageHeight);

    void process(const std::vector<uint8_t>& streamData);
    PageContent& content() { return m_content; }

private:
    PdfXref&       m_xref;
    PdfFontLoader& m_fontLoader;
    const PdfDict* m_resources;
    double         m_pageH;
    PageContent    m_content;

    std::stack<GraphicsState> m_gsStack;
    GraphicsState             m_gs;

    std::vector<PathSegment> m_currentPath;
    double m_currentX=0, m_currentY=0;
    bool   m_inText=false;

    // ---- Token ----
    struct Token {
        enum class Kind { Integer, Real, Name, String,
                          ArrayBegin, ArrayEnd, DictBegin, DictEnd, Keyword } kind;
        std::string raw;
        double numVal = 0;
    };

    std::vector<Token> m_operandStack;

    std::vector<Token> tokenise(const std::vector<uint8_t>& data);
    Token makeToken(const std::string& s);
    void  execOperator(const std::string& op);

    // Text operators
    void op_BT(); void op_ET();
    void op_Tf(const std::string& name, double size);
    void op_Tm(double a,double b,double c,double d,double e,double f);
    void op_Td(double tx,double ty); void op_TD(double tx,double ty);
    void op_Tstar();
    void op_Tj(const std::string& str);
    void op_TJ(const std::vector<Token>& arr);
    void op_Tc(double c); void op_Tw(double w); void op_Tz(double h);
    void op_TL(double l); void op_Ts(double r);

    // Path operators
    void op_m(double x,double y); void op_l(double x,double y);
    void op_c(double x1,double y1,double x2,double y2,double x3,double y3);
    void op_v(double x2,double y2,double x3,double y3);
    void op_y(double x1,double y1,double x3,double y3);
    void op_re(double x,double y,double w,double h);
    void op_h();
    void op_S(bool stroke,bool fill,bool evenOdd=false,bool close=false);
    void op_n();

    // Color operators
    void op_setGray(double g,bool stroke);
    void op_setRGB(double r,double g,double b,bool stroke);
    void op_setCMYK(double c,double m,double y,double k,bool stroke);

    // Graphics state
    void op_q(); void op_Q();
    void op_cm(double a,double b,double c,double d,double e,double f);

    // XObjects / inline images
    void op_Do(const std::string& name);
    void processImageXObject(PdfStream* stm);
    // NOTE: iterator is non-const because we advance it
    void processInlineImage(std::vector<Token>::iterator& it,
                            std::vector<Token>::iterator end);

    // Helpers
    PdfFont* resolveFont(const std::string& resName);
    double   flipY(double y) const { return m_pageH - y; }
    void     emitTextSpan(const QString& text, double x, double y, double width);

    std::vector<double> popNumbers(int count);
    std::string popString();
    std::string popName();
};

} // namespace Pdf

#endif // PDF_CONTENT_STREAM_H
