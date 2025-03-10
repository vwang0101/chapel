module FileHashing {

  /* SHA256Hash is a record storing a SHA256 hash value.
     It supports comparison and writeln.
   */
  record SHA256Hash {
    /* The actual hash value */
    var hashVal: 8*uint(32);

    /* Help `writeln` and other calls output SHA256Hash value
       in a good format.
       */

    proc writeThis(f) throws {
      for component in hashVal {
        var s = try! "%08xu".format(component);
        f.write(s);
      }
    }

    /* How to initialize an empty SHA256Hash */
    proc init() {
      // compiler adds initialization of hash to 0s
    }
    /* How to initialize a SHA256Hash from another SHA256Hash */
    proc init=(from: SHA256Hash) {
      this.hashVal = from.hashVal;
    }
    /* How to initialize a SHA256Hash from a hash tuple */
    proc init(hashVal: 8*uint(32)) {
      this.hashVal = hashVal;
    }

    /* How to compute a hash a SHA256Hash */
    proc hash() {
      return hashVal.hash();
    }
  }

  /* Called when assigning between SHA256Hash values */
  operator SHA256Hash.=(ref lhs: SHA256Hash, rhs: SHA256Hash) {
    lhs.hashVal = rhs.hashVal;
  }

  /* Helps to implement comparisons between SHA256Hash values.
     Returns -1 if a < b, 1 if a > b, or 0 if a == b.
   */
  proc compare(a: SHA256Hash, b: SHA256Hash): int {

    for i in 0..7 {
      var aa = a.hashVal[i];
      var bb = b.hashVal[i];
      if aa < bb {
        return -1;
      }
      if aa > bb {
        return 1;
      }
    }
    return 0;
  }

  operator SHA256Hash.<(a: SHA256Hash, b: SHA256Hash) {
    return compare(a, b) < 0;
  }
  operator SHA256Hash.<=(a: SHA256Hash, b: SHA256Hash) {
    return compare(a, b) <= 0;
  }
  operator SHA256Hash.==(a: SHA256Hash, b: SHA256Hash) {
    return compare(a, b) == 0;
  }
  operator SHA256Hash.!=(a: SHA256Hash, b: SHA256Hash) {
    return compare(a, b) != 0;
  }
  operator SHA256Hash.>=(a: SHA256Hash, b: SHA256Hash) {
    return compare(a, b) >= 0;
  }
  operator SHA256Hash.>(a: SHA256Hash, b: SHA256Hash) {
    return compare(a, b) > 0;
  }


  /*
     Returns the SHA256Hash for the file stored at `path`.
     May throw an error if the file could not be opened, for example.
   */
  proc computeFileHash(path: string): SHA256Hash throws {
    use IO;
    use SHA256Implementation;

    var f = open(path, iomode.r);
    var len = f.size;
    var r = f.reader(kind=iokind.big, locking=false,
                     region=0..len);


    var msg:16*uint(32); // aka 64 bytes
    var offset = 0;
    var state:SHA256State;

    // Read as many full blocks as we can
    // but don't ever read the last block in this loop
    // since we need to pass that to state.lastblock
    while offset+64 < len {
      r.read(msg);
      state.fullblock(msg);
      offset += 64;
    }


    // clear msg before last block, so unused data are zeros
    for i in 0..15 {
      msg[i] = 0;
    }

    var nbits:uint = 0;
    var msgi = 0;
    // Now handle the last 4-byte words
    while offset+4 <= len {
      r.read(msg[msgi]);
      msgi += 1;
      offset += 4;
      nbits += 32;
    }

    if offset < len {
      var lastword:uint(32);
      var byte:uint(8);
      var bytei = 0;
      // Now handle the last 1-byte portions
      while offset < len {
        r.read(byte);
        lastword <<= 8;
        lastword |= byte;
        bytei += 1;
        offset += 1;
        nbits += 8;
      }
      // Shift left so the data is in the high order bits of lastword.
      while bytei != 4 {
        lastword <<= 8;
        bytei += 1;
      }
      msg[msgi] = lastword;
    }

    var hashVal:8*uint(32) = state.lastblock(msg, nbits);

    return new SHA256Hash(hashVal);
  }

  /*
    Given a path argument, computes the realPath (that
    is the path after symbolic links are processed),
    and then computes a relative version of that to the
    current directory. Returns the result.
   */
  proc relativeRealPath(path: string): string throws {
    use Path only ;
    var currentDirectory = Path.realPath(".");
    var fullPath = Path.realPath(path);
    if !currentDirectory.endsWith("/") {
      currentDirectory += "/";
    }
    // If fullPath starts with currentDirectory, remove it
    if fullPath.startsWith(currentDirectory) {
      fullPath = fullPath[currentDirectory.size..];
    }
    return fullPath;
  }
}
