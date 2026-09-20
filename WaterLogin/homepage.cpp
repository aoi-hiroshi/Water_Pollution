#include "homepage.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

class SlideCanvas final : public QWidget
{
public:
    SlideCanvas(const QString &resourcePath,
                const QString &title,
                const QString &description,
                QWidget *parent = nullptr)
        : QWidget(parent)
        , pixmap(resourcePath)
        , title(title)
        , description(description)
    {
        setMinimumHeight(420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(title);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRect canvas = rect().adjusted(1, 1, -1, -1);
        QPainterPath clipPath;
        clipPath.addRoundedRect(QRectF(canvas), 14.0, 14.0);
        painter.setClipPath(clipPath);
        painter.fillRect(canvas, QColor("#07131f"));

        if (pixmap.isNull()) {
            painter.setPen(QColor("#dbeafe"));
            painter.drawText(canvas, Qt::AlignCenter,
                             QStringLiteral("首页展示图片加载失败"));
            return;
        }

        QSize scaledSize = pixmap.size();
        scaledSize.scale(canvas.size(), Qt::KeepAspectRatio);
        const QRect imageRect(
            canvas.left() + (canvas.width() - scaledSize.width()) / 2,
            canvas.top() + (canvas.height() - scaledSize.height()) / 2,
            scaledSize.width(), scaledSize.height());
        painter.drawPixmap(imageRect, pixmap);

        const int overlayHeight = qMin(190, qMax(128, imageRect.height() / 3));
        const QRect overlayRect(imageRect.left(),
                                imageRect.bottom() - overlayHeight + 1,
                                imageRect.width(), overlayHeight);
        QLinearGradient overlay(overlayRect.topLeft(), overlayRect.bottomLeft());
        overlay.setColorAt(0.0, QColor(5, 18, 32, 0));
        overlay.setColorAt(0.45, QColor(5, 18, 32, 150));
        overlay.setColorAt(1.0, QColor(5, 18, 32, 235));
        painter.fillRect(overlayRect, overlay);

        const int horizontalPadding = qMax(26, imageRect.width() / 32);
        QFont titleFont(QStringLiteral("Microsoft YaHei"));
        titleFont.setPixelSize(qMax(24, qMin(36, imageRect.width() / 42)));
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(Qt::white);
        const QRect titleRect = overlayRect.adjusted(
            horizontalPadding, overlayHeight / 4, -horizontalPadding, -62);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignBottom, title);

        QFont descriptionFont(QStringLiteral("Microsoft YaHei"));
        descriptionFont.setPixelSize(qMax(13, qMin(18, imageRect.width() / 78)));
        painter.setFont(descriptionFont);
        painter.setPen(QColor("#d7e8f7"));
        const QRect descriptionRect = overlayRect.adjusted(
            horizontalPadding, overlayHeight - 58, -horizontalPadding, -18);
        painter.drawText(descriptionRect,
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         description);
    }

private:
    QPixmap pixmap;
    QString title;
    QString description;
};

} // namespace

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    initUI();
}

void HomePage::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(0);

    QFrame *display = new QFrame(this);
    display->setObjectName(QStringLiteral("homeDisplay"));
    QVBoxLayout *displayLayout = new QVBoxLayout(display);
    displayLayout->setContentsMargins(0, 0, 0, 0);
    displayLayout->setSpacing(0);

    QFrame *screenHeader = new QFrame(display);
    screenHeader->setObjectName(QStringLiteral("homeScreenHeader"));
    screenHeader->setFixedHeight(58);
    QHBoxLayout *headerLayout = new QHBoxLayout(screenHeader);
    headerLayout->setContentsMargins(22, 0, 22, 0);

    QLabel *screenTitle = new QLabel(
        QStringLiteral("水污染在线监测与智能分析平台"), screenHeader);
    screenTitle->setObjectName(QStringLiteral("homeScreenTitle"));
    QLabel *screenState = new QLabel(
        QStringLiteral("●  监测场景自动轮播"), screenHeader);
    screenState->setObjectName(QStringLiteral("homeScreenState"));
    headerLayout->addWidget(screenTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(screenState);

    slideStack = new QStackedWidget(display);
    slideStack->setObjectName(QStringLiteral("homeSlideStack"));
    slideStack->addWidget(new SlideCanvas(
        QStringLiteral(":/images/monitoring_equipment.png"),
        QStringLiteral("在线监测设备与数据采集平台"),
        QStringLiteral("自动采样、在线分析与数据采集设备组成现场水质监测系统。"),
        slideStack));
    slideStack->addWidget(new SlideCanvas(
        QStringLiteral(":/images/monitoring_area_map.png"),
        QStringLiteral("污染监测区域与企业分布"),
        QStringLiteral("展示监测范围、企业位置与污染溯源分析所覆盖的重点区域。"),
        slideStack));

    QFrame *screenFooter = new QFrame(display);
    screenFooter->setObjectName(QStringLiteral("homeScreenFooter"));
    screenFooter->setFixedHeight(48);
    QHBoxLayout *footerLayout = new QHBoxLayout(screenFooter);
    footerLayout->setContentsMargins(22, 0, 22, 0);

    QLabel *runtimeState = new QLabel(
        QStringLiteral("●  现场监测画面"), screenFooter);
    runtimeState->setObjectName(QStringLiteral("homeRuntimeState"));
    slideIndicator = new QLabel(screenFooter);
    slideIndicator->setObjectName(QStringLiteral("homeSlideIndicator"));
    slideIndicator->setAlignment(Qt::AlignCenter);
    slideCounter = new QLabel(screenFooter);
    slideCounter->setObjectName(QStringLiteral("homeSlideCounter"));
    slideCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    slideCounter->setMinimumWidth(64);

    footerLayout->addWidget(runtimeState);
    footerLayout->addStretch();
    footerLayout->addWidget(slideIndicator);
    footerLayout->addStretch();
    footerLayout->addWidget(slideCounter);

    displayLayout->addWidget(screenHeader);
    displayLayout->addWidget(slideStack, 1);
    displayLayout->addWidget(screenFooter);
    rootLayout->addWidget(display, 1);

    showSlide(0);
    slideTimer = new QTimer(this);
    slideTimer->setInterval(6000);
    connect(slideTimer, &QTimer::timeout, this, [this]() {
        showSlide(currentSlide + 1);
    });
    slideTimer->start();
}

void HomePage::showSlide(int index)
{
    if (!slideStack || slideStack->count() == 0) {
        return;
    }

    const int count = slideStack->count();
    currentSlide = ((index % count) + count) % count;
    slideStack->setCurrentIndex(currentSlide);
    slideIndicator->setText(currentSlide == 0
        ? QStringLiteral("●    ○")
        : QStringLiteral("○    ●"));
    slideCounter->setText(QStringLiteral("%1 / %2")
        .arg(currentSlide + 1)
        .arg(count));
}
