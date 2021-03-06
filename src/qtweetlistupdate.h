/* Copyright 2010 Antonie Jovanoski
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Contact e-mail: Antonie Jovanoski <minimoog77_at_gmail.com>
 */

#ifndef QTWEETLISTUPDATE_H
#define QTWEETLISTUPDATE_H

#include "qtweetnetbase.h"

/**
 *  Updates the specified list.
 */
class QTWEETLIBSHARED_EXPORT QTweetListUpdate : public QTweetNetBase
{
    Q_OBJECT
public:
    QTweetListUpdate(QObject *parent = 0);
    QTweetListUpdate(OAuthTwitter *oauthTwitter, QObject *parent = 0);
    void update(qint64 user,
                qint64 list,
                const QString& name = QString(),
                bool mode = true,
                const QString& description = QString());
signals:
    /** Emits updated list */
    void parsedList(const QTweetList& list);

protected slots:
    void parseJsonFinished(const QJsonDocument &jsonDoc);
};

#endif // QTWEETLISTUPDATE_H
