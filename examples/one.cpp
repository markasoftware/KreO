class MyClass {
public:
	MyClass(int special) : special(special) { };
	int CallMe(int maybe) {
		if (maybe == special*2 - 1) {
			return maybe-1;
		}
		return maybe+1;
	};
private:
	int special;
};

int main() {
	MyClass cls1(24);
	cls1.CallMe(49);
	cls1.CallMe(47);
}
