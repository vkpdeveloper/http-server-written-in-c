# Raw HTTP Server in C

I stumbled upon this project on Codecrafters and thought, "Why the heck not just
dive in and build the whole thing?" You know, because who doesn't love a
challenge that could potentially drive them to the brink of madness?

So, this code parses the HTTP request, extracting the method, path, headers, and
even the whole body. It does a few other things too, but the parsing part is
where the real magic (and occasional hair-pulling) happens.

## Magic

Behold the mystical powers of a simple TCP socket! It takes any HTTP request
and, parses it into the http_request struct. It even knows how to read all the
headers—because why stop at just the basics when you can go full wizard?

Want to test the magic? Use the `--directory` flag to pass a directory for
testing file read and write endpoints. For reading, use `GET /files/{filename}`,
For writing, use `POST /files/{filename}`.

So go ahead, and give it a whirl. Just don't blame me if your server starts
asking for a wand and a pointy hat.
