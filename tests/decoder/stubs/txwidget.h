// decodertest stub
#ifndef TXWIDGET_H
#define TXWIDGET_H
#include "imageviewer.h"
class txWidget
{
public:
  imageViewer *getImageViewerPtr() {return &viewer;}
  imageViewer viewer;
};
#endif
