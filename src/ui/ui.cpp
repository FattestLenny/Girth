#include "ui.hpp"

void UINode::addChild(UINode node) {
    node.parent = this;
    children.emplace_back(node);
}
