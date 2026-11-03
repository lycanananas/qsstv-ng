// decodertest stub
#ifndef RXWIDGET_H
#define RXWIDGET_H
#include "imageviewer.h"
class rxWidget
{
public:
  imageViewer *getImageViewerPtr() {return &viewer;}
  imageViewer viewer;
};
#endif
