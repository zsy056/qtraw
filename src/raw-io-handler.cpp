/*
 * Copyright (C) 2012 Alberto Mardegan <info@mardy.it>
 *
 * This file is part of QtRaw.
 *
 * QtRaw is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * QtRaw is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QtRaw.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "datastream.h"
#include "raw-io-handler.h"

#include <QDebug>
#include <QImage>
#include <QVariant>

#include <libraw.h>

class RawIOHandlerPrivate
{
public:
    RawIOHandlerPrivate(RawIOHandler *qq):
        raw(0),
        q(qq)
    {}

    ~RawIOHandlerPrivate();

    bool load(QIODevice *device);

    LibRaw *raw;
    Datastream *stream;
    QSize            defaultSize;
    QSize            scaledSize;
    mutable RawIOHandler *q;
};

RawIOHandlerPrivate::~RawIOHandlerPrivate()
{
    delete raw;
    raw = 0;
    delete stream;
    stream = 0;
}

bool RawIOHandlerPrivate::load(QIODevice *device)
{
    if (device == 0) return false;

    if (raw != 0) return true;

    stream = new Datastream(device);
    raw = new LibRaw;
    if (raw->open_datastream(stream) != LIBRAW_SUCCESS) {
        delete raw;
        raw = 0;
        delete stream;
        stream = 0;
        return false;
    }

    defaultSize = QSize(raw->imgdata.sizes.width,
                        raw->imgdata.sizes.height);
    return true;
}


RawIOHandler::RawIOHandler():
    d(new RawIOHandlerPrivate(this))
{
}


RawIOHandler::~RawIOHandler()
{
    delete d;
}


bool RawIOHandler::canRead() const
{
    if (!d->load(device())) return false;

    return true;
}


QByteArray RawIOHandler::name() const
{
    return "libraw";
}


bool RawIOHandler::read(QImage *image)
{
    if (!d->load(device())) return false;

    QSize finalSize = d->scaledSize.isValid() ?
        d->scaledSize : d->defaultSize;

    const libraw_data_t &imgdata = d->raw->imgdata;
    libraw_processed_image_t *output;
    if (finalSize.width() < imgdata.thumbnail.twidth ||
        finalSize.height() < imgdata.thumbnail.theight) {
        qDebug() << "Using thumbnail";
        d->raw->unpack_thumb();
        output = d->raw->dcraw_make_mem_thumb();
    } else {
        qDebug() << "Decoding raw data";
        d->raw->unpack();
        d->raw->dcraw_process();
        output = d->raw->dcraw_make_mem_image();
    }

    QImage unscaled;
    uchar *pixels = 0;
    if (output->type == LIBRAW_IMAGE_JPEG) {
        unscaled.loadFromData(output->data, output->data_size, "JPEG");
    } else {
        int numPixels = output->width * output->height;
        int colorSize = output->bits / 8;
        int pixelSize = output->colors * colorSize;
        pixels = new uchar[numPixels * 4];
        uchar *data = output->data;
        for (int i = 0; i < numPixels; i++, data += pixelSize) {
            if (output->colors == 3) {
                pixels[i * 4] = data[2 * colorSize];
                pixels[i * 4 + 1] = data[1 * colorSize];
                pixels[i * 4 + 2] = data[0];
            } else {
                pixels[i * 4] = data[0];
                pixels[i * 4 + 1] = data[0];
                pixels[i * 4 + 2] = data[0];
            }
        }
        unscaled = QImage(pixels,
                          output->width, output->height,
                          QImage::Format_RGB32);
    }

    if (unscaled.size() != finalSize) {
        // TODO: use quality parameter to decide transformation method
        *image = unscaled.scaled(finalSize, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    } else {
        *image = unscaled;
        if (output->type == LIBRAW_IMAGE_BITMAP) {
            // make sure that the bits are copied
            uchar *b = image->bits();
            Q_UNUSED(b);
        }
    }
    d->raw->dcraw_clear_mem(output);
    delete pixels;

    return true;
}


QVariant RawIOHandler::option(ImageOption option) const
{
    switch(option) {
    case ImageFormat:
        return QImage::Format_RGB32;
    case Size:
        d->load(device());
        return d->defaultSize;
    case ScaledSize:
        return d->scaledSize;
    default:
        break;
    }
    return QVariant();
}


void RawIOHandler::setOption(ImageOption option, const QVariant & value)
{
    switch(option) {
    case ScaledSize:
        d->scaledSize = value.toSize();
        break;
    default:
        break;
    }
}


bool RawIOHandler::supportsOption(ImageOption option) const
{
    switch (option)
    {
    case ImageFormat:
    case Size:
    case ScaledSize:
        return true;
    default:
        break;
    }
    return false;
}