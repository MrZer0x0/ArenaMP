#include "Crypto.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace ArenaCrypto {
QByteArray randomBytes(int count) {
    QByteArray out(count, Qt::Uninitialized);
    if (count > 0 && RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), count) != 1) return {};
    return out;
}
QByteArray hmacSha256(const QByteArray& key, const QByteArray& data) {
    unsigned int len=0; unsigned char out[EVP_MAX_MD_SIZE];
    if (!HMAC(EVP_sha256(), key.constData(), key.size(),
              reinterpret_cast<const unsigned char*>(data.constData()), data.size(), out, &len)) return {};
    return QByteArray(reinterpret_cast<const char*>(out), int(len));
}
bool constantTimeEqual(const QByteArray& a, const QByteArray& b) {
    return a.size()==b.size() && CRYPTO_memcmp(a.constData(), b.constData(), size_t(a.size()))==0;
}
bool aes256GcmEncrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& aad,
                      const QByteArray& clear, QByteArray& cipherAndTag) {
    if (key.size()!=32 || nonce.size()!=12) return false;
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new(); if(!ctx) return false;
    QByteArray cipher(clear.size(), Qt::Uninitialized); int len=0,total=0; unsigned char tag[16]; bool ok=false;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)!=1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr)!=1) goto done;
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
        reinterpret_cast<const unsigned char*>(key.constData()), reinterpret_cast<const unsigned char*>(nonce.constData()))!=1) goto done;
    if (!aad.isEmpty() && EVP_EncryptUpdate(ctx,nullptr,&len,reinterpret_cast<const unsigned char*>(aad.constData()),aad.size())!=1) goto done;
    if (!clear.isEmpty() && EVP_EncryptUpdate(ctx,reinterpret_cast<unsigned char*>(cipher.data()),&len,
        reinterpret_cast<const unsigned char*>(clear.constData()),clear.size())!=1) goto done;
    total=len;
    if (EVP_EncryptFinal_ex(ctx,reinterpret_cast<unsigned char*>(cipher.data())+total,&len)!=1) goto done;
    total+=len; cipher.resize(total);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag)!=1) goto done;
    cipherAndTag=cipher+QByteArray(reinterpret_cast<const char*>(tag),16); ok=true;
done: EVP_CIPHER_CTX_free(ctx); return ok;
}
bool aes256GcmDecrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& aad,
                      const QByteArray& cipherAndTag, QByteArray& clear) {
    if (key.size()!=32 || nonce.size()!=12 || cipherAndTag.size()<16) return false;
    const QByteArray cipher=cipherAndTag.left(cipherAndTag.size()-16), tag=cipherAndTag.right(16);
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new(); if(!ctx) return false;
    QByteArray out(cipher.size(), Qt::Uninitialized); int len=0,total=0; bool ok=false;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)!=1) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr)!=1) goto done;
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
        reinterpret_cast<const unsigned char*>(key.constData()), reinterpret_cast<const unsigned char*>(nonce.constData()))!=1) goto done;
    if (!aad.isEmpty() && EVP_DecryptUpdate(ctx,nullptr,&len,reinterpret_cast<const unsigned char*>(aad.constData()),aad.size())!=1) goto done;
    if (!cipher.isEmpty() && EVP_DecryptUpdate(ctx,reinterpret_cast<unsigned char*>(out.data()),&len,
        reinterpret_cast<const unsigned char*>(cipher.constData()),cipher.size())!=1) goto done;
    total=len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<char*>(tag.constData()))!=1) goto done;
    if (EVP_DecryptFinal_ex(ctx,reinterpret_cast<unsigned char*>(out.data())+total,&len)!=1) goto done;
    total+=len; out.resize(total); clear=out; ok=true;
done: EVP_CIPHER_CTX_free(ctx); return ok;
}
}
