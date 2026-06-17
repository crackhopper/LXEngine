module;
#include <limits>
#include <vector>

export module LX_New_Common.Math:Shape;
import :Vec;
import :Mat;

export namespace LX_New_Common {

struct Rect2Di {
  i32 x = 0;
  i32 y = 0;
  u32 width = 0;
  u32 height = 0;

  Rect2Di() = default;
  Rect2Di(i32 xVal, i32 yVal, u32 w, u32 h)
      : x(xVal), y(yVal), width(w), height(h) {}

  [[nodiscard]] bool isEmpty() const { return width == 0 || height == 0; }

  [[nodiscard]] i32 right() const { return x + static_cast<i32>(width); }
  [[nodiscard]] i32 bottom() const { return y + static_cast<i32>(height); }

  [[nodiscard]] bool contains(i32 px, i32 py) const {
    return px >= x && px < right() && py >= y && py < bottom();
  }
};

/**
 * @brief 轴对齐包围盒 (Axis-Aligned Bounding Box)
 */
struct BoundingBox {
  Vec3f min;
  Vec3f max;

  // 默认构造创建一个“无效”的包围盒，方便后续通过 merge 扩展
  BoundingBox() {
    float inf = std::numeric_limits<float>::infinity();
    min = Vec3f(inf, inf, inf);
    max = Vec3f(-inf, -inf, -inf);
  }

  BoundingBox(const Vec3f &minPos, const Vec3f &maxPos)
      : min(minPos), max(maxPos) {}

  /**
   * @brief 检查包围盒是否有效
   */
  bool isValid() const {
    return max.x >= min.x && max.y >= min.y && max.z >= min.z;
  }

  /**
   * @brief 获取中心点
   */
  Vec3f getCenter() const { return (min + max) * 0.5f; }

  /**
   * @brief 获取长宽高
   */
  Vec3f getSize() const { return max - min; }

  /**
   * @brief 合并一个点到包围盒中
   */
  void merge(const Vec3f &point) {
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
  }

  /**
   * @brief 合并另一个包围盒
   */
  void merge(const BoundingBox &other) {
    if (!other.isValid())
      return;
    merge(other.min);
    merge(other.max);
  }

  /**
   * @brief 对包围盒应用仿射变换
   * 注意：变换后的 AABB 必须重新包围原始 AABB 的所有 8 个顶点
   */
  BoundingBox transformed(const Mat4f &matrix) const {
    if (!isValid())
      return *this;

    Vec4f corners[8] = {{min.x, min.y, min.z, 1}, {max.x, min.y, min.z, 1},
                        {min.x, max.y, min.z, 1}, {max.x, max.y, min.z, 1},
                        {min.x, min.y, max.z, 1}, {max.x, min.y, max.z, 1},
                        {min.x, max.y, max.z, 1}, {max.x, max.y, max.z, 1}};

    BoundingBox result;
    for (int i = 0; i < 8; ++i) {
      // 假设 Mat4f 乘法已实现，处理点变换
      auto transformedPoint = matrix * corners[i];
      result.merge(transformedPoint.toVec3());
    }
    return result;
  }

  /**
   * @brief 判断点是否在盒内
   */
  bool contains(const Vec3f &point) const {
    return (point.x >= min.x && point.x <= max.x) &&
           (point.y >= min.y && point.y <= max.y) &&
           (point.z >= min.z && point.z <= max.z);
  }

  /**
   * @brief 判断两个 AABB 是否相交
   */
  bool intersects(const BoundingBox &other) const {
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
  }
};

} // namespace LX_New_Common