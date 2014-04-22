/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>

#include <FdsCrypto.h>

namespace fds {
namespace hash {

// MD5 implementations
Md5::Md5() {
}

Md5::~Md5() {
}

std::string
Md5::getAlgorithmName() const {
    return "MD5";
}

void
Md5::calculateDigest(byte *digest,
                     const byte *input,
                     size_t length) {
    calcDigestStatic(digest, input, length);
}

void
Md5::calcDigestStatic(byte *digest,
                      const byte *input,
                      size_t length) {
    CryptoPP::Weak::MD5().CalculateDigest(digest,
                                    input,
                                    length);
}

void
Md5::update(const byte *input, size_t length) {
    myHash.Update(input, length);
}

void
Md5::final(byte *digest) {
    myHash.Final(digest);
}

void
Md5::restart() {
    myHash.Restart();
}

// SHA1 implementations
Sha1::Sha1() {
}

Sha1::~Sha1() {
}

std::string
Sha1::getAlgorithmName() const {
    return "SHA1";
}

void
Sha1::calculateDigest(byte *digest,
                          const byte *input,
                          size_t length) {
    calcDigestStatic(digest, input, length);
}

void
Sha1::calcDigestStatic(byte *digest,
                           const byte *input,
                           size_t length) {
    CryptoPP::SHA1().CalculateDigest(digest,
                                     input,
                                     length);
}

void
Sha1::update(const byte *input, size_t length) {
    myHash.Update(input, length);
}

void
Sha1::final(byte *digest) {
    myHash.Final(digest);
}

void
Sha1::restart() {
    myHash.Restart();
}

// SHA256 implementations
Sha256::Sha256() {
}

Sha256::~Sha256() {
}

std::string
Sha256::getAlgorithmName() const {
    return "SHA256";
}

void
Sha256::calculateDigest(byte *digest,
                          const byte *input,
                          size_t length) {
    calcDigestStatic(digest, input, length);
}

void
Sha256::calcDigestStatic(byte *digest,
                           const byte *input,
                           size_t length) {
    CryptoPP::SHA256().CalculateDigest(digest,
                                     input,
                                     length);
}

void
Sha256::update(const byte *input, size_t length) {
    myHash.Update(input, length);
}

void
Sha256::final(byte *digest) {
    myHash.Final(digest);
}

void
Sha256::restart() {
    myHash.Restart();
}

// Murmur3 implementation
Murmur3::Murmur3() {
}

Murmur3::~Murmur3() {
}

std::string
Murmur3::getAlgorithmName() const {
    return "MURMUR3";
}

void
Murmur3::calculateDigest(byte *digest,
                         const byte *input,
                         size_t length) {
    calcDigestStatic(digest, input, length);
}

void
Murmur3::calcDigestStatic(byte *digest,
                         const byte *input,
                         size_t length) {
    MurmurHash3_x64_128(input,
                        length,
                        0,
                        digest);
}

}  // namespace hash

namespace checksum {

// SHA256 implementations
Crc32::Crc32() {
}

Crc32::~Crc32() {
}

std::string
Crc32::getAlgorithmName() const {
    return "CRC32";
}

void
Crc32::calculateDigest(byte *digest,
                       const byte *input,
                       size_t length) {
    calcDigestStatic(digest, input, length);
}

void
Crc32::calcDigestStatic(byte *digest,
                        const byte *input,
                        size_t length) {
    CryptoPP::CRC32().CalculateDigest(digest,
                                      input,
                                      length);
}

void
Crc32::update(const byte *input, size_t length) {
    myHash.Update(input, length);
}

void
Crc32::final(byte *digest) {
    myHash.Final(digest);
}

void
Crc32::restart() {
    myHash.Restart();
}
}  // namespace checksum

}  // namespace fds
