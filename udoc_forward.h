#ifndef UDOC_FORWARD_H
#define UDOC_FORWARD_H

// This header contains ONLY forward declarations.
// Include this everywhere you need a pointer/reference but not the full definition.

namespace UDoc {

// Core
class Document;
class Tab;
class Page;
class Story;
class Block;
class Element;
class UDocObject;

// Content types (for variant — need full definition in headers that use them)
struct TextBlockContent;
struct HeadingContent;
struct ListContent;
struct Image;
struct VectorGraphic;
struct Media;
struct Formula;
struct Link;
struct Bookmark;
struct Field;
struct Comment;
struct Group;

// Table (separate to avoid circular include)
class Table;
struct TableCell;

// Properties
struct CharacterProperties;
struct ParagraphProperties;
struct PageLayout;
struct AnchorProperties;

// Inline
struct InlineRun;
struct EmbeddedObject;

} // namespace UDoc

#endif // UDOC_FORWARD_H
