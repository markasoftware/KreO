/**
 * This example is to demonstrate Kreo's static analysis capabilities. There are multiple classes with multiple methods. Only some branches will be executed, and full coverage can only be reached by static analysis. The classes in this file have explicit destructors, so dynamic analysis should be able to recover the hierarchy.
 */

#include <cstdlib>
#include <vector>

class MyClass {
public:
    MyClass(int x) : x(x) { }

    virtual ~MyClass() { }

    int XMarksTheSpot() {
        return x*2;
    }

    int XFXRadeonRX7900XTX() {
        return x / 2 + 5;
    }

    int XXX() {
        return x*x*x;
    }

    int TooXyForMilan() {
        return x - 1;
    }

    bool IsXyEnoughForMyShirt() {
        return x*x > 88;
    }

    

    // static analysis should be able to catch one branch even if only the other is executed.
    int Ynng(int y) {
        if (y%3 == 0) {
            return XMarksTheSpot();
        } else {
            return XFXRadeonRX7900XTX();
        }
    }

    // ensure that dataflow survives a merge.
    int Merge(int z) {
        if (z > 8) {
            int res;
            if (z % 7 == 0) {
                res = XXX();
            } else {
                res = TooXyForMilan();
            }
            return res+5;
        } else {
            return 99;
        }
    }

protected:
    int x;
    // force destructor
    std::vector<int> crap;
};

// subclass, to see if things get assigned to parent classes or not.
class MySubClass : public MyClass {
public:
    using MyClass::MyClass;

protected:
    // force another destructor
    std::vector<int> moreCrap;
};

int main() {
    MyClass blop(42);
    blop.Ynng(3);
    blop.Merge(4);
}
