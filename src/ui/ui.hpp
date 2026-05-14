#ifndef GIRTH_UI_H
#define GIRTH_UI_H

#include <vector>

enum class UIPositionType {
    ABSOLUTE,
    RELATIVE,
};

enum class UISizeType {
    FIXED,
    FIT,
    GROW,
};

enum class UILayoutDirection {
    NONE,
    LEFT_TO_RIGHT,
    TOP_TO_BOTTOM,
};

enum class UILayoutAlignment {
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    CENTER,
};

class UINode {
public:
    // Core properties
    bool visible;

    UIPositionType positionType;
    double xPosition, yPosition;

    UISizeType widthType, heightType;
    double width, height;

    UILayoutDirection layoutDirection;
    UILayoutAlignment layoutAlignmentX, layoutAlignmentY;

    // Style properties
    double paddingLeft;
    double paddingRight;
    double paddingTop;
    double paddingBottom;
    double childGap;

private:
    UINode* parent;
    // TODO: Turn this into vector<UINode*> if needed.
    std::vector<UINode> children;
};

#endif
