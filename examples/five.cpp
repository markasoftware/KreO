#include <iostream>

class baz{
    public:
    baz() {
        one();
        std::cout << "baz()" << std::endl;
    }
    ~baz() {
        two();
        std::cout << "~baz()" << std::endl;
    }

    void one() {
        std::cout << "one()" << std::endl;
    }

    void two() {
        std::cout << "two()" << std::endl;
    }
};

class foo{
public:
    foo() {
        three();
        std::cout << "foo()" << std::endl;
    }
    ~foo() {
        four();
        std::cout << "~foo()" << std::endl;
    }

    void three() {
        std::cout << "three()" << std::endl;
    }

    void four() {
        std::cout << "four()" << std::endl;
    }
    
    private:
    baz b;
};

class bar : foo {
public:
    bar() {
        five();
        std::cout << "bar()" << std::endl;
    }
    ~bar() {
        six();
        std::cout << "~bar()" << std::endl;
    }

    void five() {
        std::cout << "five()" << std::endl;
    }

    void six() {
        std::cout << "six()" << std::endl;
    }
};

int main()
{
    bar b;
    return 0;
}
