// ============================================================================
// composition.cpp
//
// The Strategy pattern from Design Patterns (GoF, ch. 5), complete and
// runnable: Composition delegates line-breaking to a Compositor, and the same
// components get laid out three different ways with no change to the document.
//
// The book leaves two things as comments -- "prepare the arrays" and "lay out
// components according to breaks" -- and never shows a Component hierarchy or
// a rendering target. Both are filled in here. Output is an ASCII canvas so
// you can see the different break decisions.
//
// Build: g++ -std=c++17 -O2 -Wall -Wextra -o composition composition.cpp
// Run:   ./composition
// ============================================================================

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Coord = int;

constexpr Coord EmWidth    = 10;  // every glyph in our fake font is one em
constexpr Coord LineHeight = 12;

// ============================================================================
// Window -- a character grid standing in for a real drawing surface.
// x/y arrive in layout units and get snapped to the nearest cell. One caveat
// of that quantization: on an overfull line a shrunk space can round onto the
// following glyph's cell, so two words touch. A real renderer would just draw
// a narrower space.
// ============================================================================
class Window {
public:
    Window(int cols, int rows)
        : _cols(cols), _rows(rows), _grid(rows, std::string(cols, ' ')) {}

    void Clear() {
        for (auto& row : _grid) row.assign(_cols, ' ');
    }

    void DrawChar(char c, Coord x, Coord y) {
        const int row = y / LineHeight;
        if (row < 0 || row >= _rows) return;
        const int col = (x + EmWidth / 2) / EmWidth;  // round to nearest column
        if (col >= 0 && col < _cols) _grid[row][col] = c;
    }

    // Rules mark the edges of the measure. Anything that spills past the right
    // rule is an overfull line -- shown rather than clipped.
    void Print(int lines, int measureCols) const {
        const std::string bar(measureCols, '-');
        std::cout << "    +" << bar << "+\n";
        for (int r = 0; r < std::min(lines, _rows); ++r) {
            const std::string& row = _grid[r];
            std::string body = row.substr(0, measureCols);
            std::string tail = row.substr(measureCols);
            tail.erase(tail.find_last_not_of(' ') + 1);
            std::cout << "    |" << body << "|" << tail << "\n";
        }
        std::cout << "    +" << bar << "+\n";
    }

private:
    int                      _cols;
    int                      _rows;
    std::vector<std::string> _grid;
};

// ============================================================================
// Component -- anything that can sit on a line. Each one advertises a natural
// size plus how far it will stretch and shrink.
// ============================================================================
class Component {
public:
    virtual ~Component() = default;

    virtual void Draw(Window*) const {}

    Coord Natural() const { return _natural; }
    Coord Stretch() const { return _stretch; }
    Coord Shrink()  const { return _shrink;  }

    // Glue that lands on a line break is thrown away (TeX's "discardable"
    // rule) -- otherwise every line after the first would start with a space.
    virtual bool Discardable() const { return false; }

    void  SetPlacement(Coord x, Coord y, Coord w) { _x = x; _y = y; _width = w; }
    Coord Width() const { return _width; }

protected:
    Component(Coord natural, Coord stretch, Coord shrink)
        : _natural(natural), _stretch(stretch), _shrink(shrink) {}

    Coord _natural, _stretch, _shrink;
    Coord _x = 0, _y = 0, _width = 0;
};

class Character : public Component {
public:
    explicit Character(char c) : Component(EmWidth, 0, 0), _c(c) {}
    void Draw(Window* w) const override { w->DrawChar(_c, _x, _y); }

private:
    char _c;
};

// Interword space: the only elastic thing on the line, and the only legal
// place to break.
class Glue : public Component {
public:
    Glue() : Component(EmWidth, 6, 3) {}
    bool Discardable() const override { return true; }
    // Draw() inherited: whitespace paints nothing.
};

// ============================================================================
// Compositor -- the Strategy interface.
//
// Note what it does NOT receive: a Component. It sees three parallel arrays of
// numbers and writes back break positions. breaks[i] is the index one past the
// last component on line i, and line i+1 starts there.
//
// Because only Glue has nonzero stretch, a compositor can still tell where the
// legal breakpoints are -- stretch[i] > 0 means "component i is a space."
// ============================================================================
class Compositor {
public:
    virtual ~Compositor() = default;
    virtual const char* Name() const = 0;

    virtual int Compose(const Coord natural[], const Coord stretch[],
                        const Coord shrink[], int componentCount,
                        Coord lineWidth, int breaks[]) = 0;

protected:
    Compositor() = default;
};

// Greedy first-fit, shared by SimpleCompositor and used as TeX's fallback.
static int GreedyCompose(const Coord natural[], const Coord stretch[],
                         int n, Coord lineWidth, int breaks[]) {
    int lines = 0;
    int start = 0;

    while (start < n) {
        Coord used     = 0;
        int   lastGlue = -1;
        int   i        = start;

        for (; i < n; ++i) {
            if (used + natural[i] > lineWidth && lastGlue >= 0) break;
            used += natural[i];
            if (stretch[i] > 0) lastGlue = i;
        }

        if (i >= n) { breaks[lines++] = n; break; }  // last line
        breaks[lines++] = lastGlue + 1;              // glue ends the line
        start = lastGlue + 1;
    }
    return lines;
}

// ---------------------------------------------------------------------------
class SimpleCompositor : public Compositor {
public:
    const char* Name() const override { return "SimpleCompositor (greedy first-fit)"; }

    int Compose(const Coord natural[], const Coord stretch[], const Coord[],
                int componentCount, Coord lineWidth, int breaks[]) override {
        return GreedyCompose(natural, stretch, componentCount, lineWidth, breaks);
    }
};

// ---------------------------------------------------------------------------
// Global optimization over the whole paragraph: a DP that minimizes total
// demerits. This is why line-breaking is a strategy and not a flag -- it is a
// completely different algorithm, not a tweak to the greedy one.
// ---------------------------------------------------------------------------
class TeXCompositor : public Compositor {
public:
    const char* Name() const override { return "TeXCompositor (global optimum)"; }

    int Compose(const Coord natural[], const Coord stretch[],
                const Coord shrink[], int n,
                Coord lineWidth, int breaks[]) override {
        constexpr long long INF          = 1LL << 60;
        constexpr long long LinePenalty  = 10;
        constexpr long long MaxBadness   = 10000;

        std::vector<long long> cost(n + 1, INF);
        std::vector<int>       from(n + 1, -1);
        cost[0] = 0;

        for (int end = 1; end <= n; ++end) {
            // A line may only end here if this is the paragraph's end or the
            // preceding component is glue.
            if (end != n && stretch[end - 1] <= 0) continue;

            int e = end;                                     // drop the glue
            while (e > 0 && stretch[e - 1] > 0) --e;
            if (e <= 0) continue;

            Coord nat = 0, str = 0, shr = 0;
            for (int start = e - 1; start >= 0; --start) {
                nat += natural[start];
                str += stretch[start];
                shr += shrink[start];

                if (nat - shr > lineWidth) break;   // overfull even fully squeezed
                if (cost[start] == INF) continue;   // not a reachable line start

                long long badness = 0;
                if (end != n) {                     // last line may run short
                    const Coord slack   = lineWidth - nat;
                    const Coord elastic = std::max(slack >= 0 ? str : shr, 1);
                    const long long r   = (100LL * std::abs(slack)) / elastic;
                    badness = std::min(MaxBadness, r * r * r / 10000);
                }

                const long long demerits = (badness + LinePenalty) * (badness + LinePenalty);
                if (cost[start] + demerits < cost[end]) {
                    cost[end] = cost[start] + demerits;
                    from[end] = start;
                }
            }
        }

        if (n == 0) return 0;
        if (cost[n] == INF)   // e.g. a single word wider than the measure
            return GreedyCompose(natural, stretch, n, lineWidth, breaks);

        std::vector<int> rev;                       // walk the chain back
        for (int at = n; at > 0; at = from[at]) rev.push_back(at);
        const int lines = static_cast<int>(rev.size());
        for (int i = 0; i < lines; ++i) breaks[i] = rev[lines - 1 - i];
        return lines;
    }
};

// ---------------------------------------------------------------------------
// Fixed intervals, for laying icons out in a grid. Ignores every measurement
// it is handed -- including the measure itself.
// ---------------------------------------------------------------------------
class ArrayCompositor : public Compositor {
public:
    explicit ArrayCompositor(int interval) : _interval(interval) {}
    const char* Name() const override { return "ArrayCompositor (fixed interval)"; }

    int Compose(const Coord[], const Coord[], const Coord[],
                int componentCount, Coord, int breaks[]) override {
        int lines = 0;
        for (int i = _interval; i < componentCount; i += _interval)
            breaks[lines++] = i;
        if (componentCount > 0) breaks[lines++] = componentCount;
        return lines;
    }

private:
    int _interval;
};

// ============================================================================
// Composition -- the Strategy context. Owns the components, gathers their
// measurements, delegates the decision, then applies the result.
// ============================================================================
class Composition {
public:
    // Takes ownership of the compositor (GoF's raw-pointer client code, minus
    // the leak).
    Composition(Compositor* c, Coord lineWidth)
        : _compositor(c), _lineWidth(lineWidth) {}

    void Insert(Component* c, int index) {           // takes ownership
        _components.insert(_components.begin() + index,
                           std::unique_ptr<Component>(c));
    }
    void Append(Component* c) { Insert(c, Count()); }

    int Count() const { return static_cast<int>(_components.size()); }

    void SetCompositor(Compositor* c) { _compositor.reset(c); }
    const Compositor* GetCompositor() const { return _compositor.get(); }

    void Repair();
    void Draw(Window* w) const {
        for (const auto& c : _components) c->Draw(w);
    }

    int LineCount() const { return static_cast<int>(_lineBreaks.size()); }
    const std::vector<Coord>& LineSlack() const { return _lineSlack; }

private:
    void LayOutLine(int first, int last, int lineNumber);

    std::unique_ptr<Compositor>             _compositor;
    std::vector<std::unique_ptr<Component>> _components;
    Coord                                   _lineWidth;
    std::vector<int>                        _lineBreaks;
    std::vector<Coord>                      _lineSlack;
};

void Composition::Repair() {
    const int n = Count();
    _lineBreaks.clear();
    _lineSlack.clear();
    if (n == 0) return;

    // --- "prepare the arrays with the desired component sizes" -------------
    std::vector<Coord> natural(n), stretch(n), shrink(n);
    for (int i = 0; i < n; ++i) {
        natural[i] = _components[i]->Natural();
        stretch[i] = _components[i]->Stretch();
        shrink[i]  = _components[i]->Shrink();
    }

    // --- "determine where the breaks are" ----------------------------------
    _lineBreaks.assign(n + 1, 0);
    const int lineCount = _compositor->Compose(natural.data(), stretch.data(),
                                               shrink.data(), n,
                                               _lineWidth, _lineBreaks.data());
    _lineBreaks.resize(lineCount);

    // --- "lay out components according to breaks" --------------------------
    int first = 0;
    for (int line = 0; line < lineCount; ++line) {
        LayOutLine(first, _lineBreaks[line], line);
        first = _lineBreaks[line];
    }
}

// Distributes the line's slack across its components in proportion to how
// elastic each one is. This is the only place a Component gets touched.
void Composition::LayOutLine(int first, int last, int lineNumber) {
    // Glue at either edge of a line is discarded.
    while (last > first && _components[last - 1]->Discardable()) {
        _components[--last]->SetPlacement(0, 0, 0);
    }
    while (first < last && _components[first]->Discardable()) {
        _components[first++]->SetPlacement(0, 0, 0);
    }

    Coord naturalSum = 0, stretchSum = 0, shrinkSum = 0;
    for (int i = first; i < last; ++i) {
        naturalSum += _components[i]->Natural();
        stretchSum += _components[i]->Stretch();
        shrinkSum  += _components[i]->Shrink();
    }

    const Coord slack = _lineWidth - naturalSum;
    const Coord y     = lineNumber * LineHeight;
    _lineSlack.push_back(slack);

    Coord x = 0;
    for (int i = first; i < last; ++i) {
        Component* c     = _components[i].get();
        Coord      width = c->Natural();

        if (slack > 0 && stretchSum > 0)
            width += slack * c->Stretch() / stretchSum;
        else if (slack < 0 && shrinkSum > 0)
            width += slack * c->Shrink() / shrinkSum;   // slack is negative

        c->SetPlacement(x, y, width);
        x += width;
    }
}

// ============================================================================
// Demo
// ============================================================================
namespace {

void FillParagraph(Composition& doc, const std::string& text) {
    for (char ch : text) {
        if (ch == ' ') doc.Append(new Glue);
        else           doc.Append(new Character(ch));
    }
}

void Show(Composition& doc, Window& win, int measureCols) {
    doc.Repair();
    win.Clear();
    doc.Draw(&win);

    std::cout << "\n" << doc.GetCompositor()->Name()
              << "  --  " << doc.LineCount() << " lines\n";
    win.Print(doc.LineCount(), measureCols);

    std::cout << "    slack per line (ems):";
    for (Coord s : doc.LineSlack()) std::cout << " " << s / EmWidth;
    std::cout << "\n";
}

}  // namespace

int main() {
    const std::string text =
        "Strategy lets an algorithm vary independently from the clients that "
        "use it. The paragraph below is broken into lines three separate ways, "
        "and not one component knows the difference. Only the compositor "
        "changes.";

    constexpr Coord MeasureUnits = 260;                    // line width
    constexpr int   MeasureCols  = MeasureUnits / EmWidth; // 40 columns

    Composition doc(new SimpleCompositor, MeasureUnits);
    FillParagraph(doc, text);

    Window win(MeasureCols + 4, 40);

    std::cout << "measure: " << MeasureCols << " columns, "
              << doc.Count() << " components\n";

    Show(doc, win, MeasureCols);

    doc.SetCompositor(new TeXCompositor);      // same components, new algorithm
    Show(doc, win, MeasureCols);

    doc.SetCompositor(new ArrayCompositor(18));
    Show(doc, win, MeasureCols);

    return 0;
}