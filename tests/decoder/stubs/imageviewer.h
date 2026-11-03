// decodertest stub: the received image is a plain QImage
#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H
#include <QImage>
#include <QColor>

class imageViewer
{
public:
  QImage image;
  QRgb *getScanLineAddress(int line) {return (QRgb *)image.scanLine(line);}
  bool hasValidImage() {return !image.isNull();}
  QImage *getImagePtr() {return &image;}
};
#endif
