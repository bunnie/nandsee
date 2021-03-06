#ifndef HISTOGRAMVIEW_H
#define HISTOGRAMVIEW_H

#include <QGraphicsView>
#include <QList>
#include <QPair>

class QPlainTextEdit;

class HistogramView : public QWidget
{
    Q_OBJECT

private:
	QByteArray data;
	unsigned int mouseY;
	static const unsigned int MAX_VALUE = 256;

	QPlainTextEdit *statsOutput;

	QList<QPair<int,int> > buckets;
	QList<QPair<int,int> > sortedBuckets;

public:
    explicit HistogramView(QWidget *parent = 0);
	void setData(const QByteArray &newData);
	void setStatsOutput(QPlainTextEdit *newStatsOutput);

signals:
    
public slots:
    
protected:
    void paintEvent(QPaintEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
};

#endif // HISTOGRAMVIEW_H
