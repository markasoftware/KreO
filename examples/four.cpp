class MyClass {
public:
    void CallMe() {
        int *i = new int();
    }
};

int main() {
    MyClass cls;

    cls.CallMe();
}
