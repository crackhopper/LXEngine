namespace infra {

class Gui {
public:
private:
  struct Impl; // PImpl 隐藏 SDL/GLFW
  Impl *pImpl;
};

} // namespace infra