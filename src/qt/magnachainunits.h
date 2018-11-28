// Copyright (c) 2011-2016 The MagnaChain Core developers
// Copyright (c) 2016-2019 The MagnaChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MAGNACHAIN_QT_MAGNACHAINUNITS_H
#define MAGNACHAIN_QT_MAGNACHAINUNITS_H

#include "misc/amount.h"

#include <QAbstractListModel>
#include <QString>

// U+2009 THIN SPACE = UTF-8 E2 80 89
#define REAL_THIN_SP_CP 0x2009
#define REAL_THIN_SP_UTF8 "\xE2\x80\x89"
#define REAL_THIN_SP_HTML "&thinsp;"

// U+200A HAIR SPACE = UTF-8 E2 80 8A
#define HAIR_SP_CP 0x200A
#define HAIR_SP_UTF8 "\xE2\x80\x8A"
#define HAIR_SP_HTML "&#8202;"

// U+2006 SIX-PER-EM SPACE = UTF-8 E2 80 86
#define SIXPEREM_SP_CP 0x2006
#define SIXPEREM_SP_UTF8 "\xE2\x80\x86"
#define SIXPEREM_SP_HTML "&#8198;"

// U+2007 FIGURE SPACE = UTF-8 E2 80 87
#define FIGURE_SP_CP 0x2007
#define FIGURE_SP_UTF8 "\xE2\x80\x87"
#define FIGURE_SP_HTML "&#8199;"

// QMessageBox seems to have a bug whereby it doesn't display thin/hair spaces
// correctly.  Workaround is to display a space in a small font.  If you
// change this, please test that it doesn't cause the parent span to start
// wrapping.
#define HTML_HACK_SP "<span style='white-space: nowrap; font-size: 6pt'> </span>"

// Define THIN_SP_* variables to be our preferred type of thin space
#define THIN_SP_CP   REAL_THIN_SP_CP
#define THIN_SP_UTF8 REAL_THIN_SP_UTF8
#define THIN_SP_HTML HTML_HACK_SP

/** MagnaChain unit definitions. Encapsulates parsing and formatting
   and serves as list model for drop-down selection boxes.
*/
class MagnaChainUnits: public QAbstractListModel
{
    Q_OBJECT

public:
    explicit MagnaChainUnits(QObject *parent);

    /** MagnaChain units.
      @note Source: https://en.magnachain.it/wiki/Units . Please add only sensible ones
     */
    enum Unit
    {
        BTC,
        mBTC,
        uBTC
    };

    enum SeparatorStyle
    {
        separatorNever,
        separatorStandard,
        separatorAlways
    };

    //! @name Static API
    //! Unit conversion and formatting
    ///@{

    //! Get list of units, for drop-down box
    static QList<Unit> availableUnits();
    //! Is unit ID valid?
    static bool valid(int unit);
    //! Short name
    static QString name(int unit);
    //! Longer description
    static QString description(int unit);
    //! Number of Satoshis (1e-8) per unit
    static qint64 factor(int unit);
    //! Number of decimals left
    static int decimals(int unit);
    //! Format as string
    static QString format(int unit, const MCAmount& amount, bool plussign=false, SeparatorStyle separators=separatorStandard);
    //! Format as string (with unit)
    static QString formatWithUnit(int unit, const MCAmount& amount, bool plussign=false, SeparatorStyle separators=separatorStandard);
    //! Format as HTML string (with unit)
    static QString formatHtmlWithUnit(int unit, const MCAmount& amount, bool plussign=false, SeparatorStyle separators=separatorStandard);
    //! Parse string to coin amount
    static bool parse(int unit, const QString &value, MCAmount *val_out);
    //! Gets title for amount column including current display unit if optionsModel reference available */
    static QString getAmountColumnTitle(int unit);
    ///@}

    //! @name AbstractListModel implementation
    //! List model for unit drop-down selection box.
    ///@{
    enum RoleIndex {
        /** Unit identifier */
        UnitRole = Qt::UserRole
    };
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    ///@}

    static QString removeSpaces(QString text)
    {
        text.remove(' ');
        text.remove(QChar(THIN_SP_CP));
#if (THIN_SP_CP != REAL_THIN_SP_CP)
        text.remove(QChar(REAL_THIN_SP_CP));
#endif
        return text;
    }

    //! Return maximum number of base units (Satoshis)
    static MCAmount maxMoney();

private:
    QList<MagnaChainUnits::Unit> unitlist;
};
typedef MagnaChainUnits::Unit MagnaChainUnit;

#endif // MAGNACHAIN_QT_MAGNACHAINUNITS_H
