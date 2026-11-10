#pragma once
#include <QByteArray>

namespace ArenaCrypto {
QByteArray randomBytes(int count);
QByteArray hmacSha256(const QByteArray& key, const QByteArray& data);
bool aes256GcmEncrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& aad,
                      const QByteArray& clear, QByteArray& cipherAndTag);
bool aes256GcmDecrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& aad,
                      const QByteArray& cipherAndTag, QByteArray& clear);
bool constantTimeEqual(const QByteArray& a, const QByteArray& b);
}
