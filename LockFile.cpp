#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <sstream>
#include <cstdlib>

#include <Role/LockFile.h>
#include <Role/GetText.h>

LockFile::LockFile(const std::string &file): lockfile(file)
{
	pid_t pid = getpid();
	std::ostringstream temp;
	temp << pid;
	std::string uniq = temp.str();
	lockfile += ".lock";

	DoLock(file, uniq);
}

LockFile::~LockFile()
{
	unlink(lockfile.c_str());
}

bool LockFile::CheckLinkCount(const std::string &file)
{
	struct stat sb;

	if (stat(file.c_str(), &sb) != 0)
		return false;

	if (sb.st_nlink != 2)
		return false;

	return true;
}

void LockFile::DoLock(const std::string &file, const std::string &uniq)
{
	int fd;
	std::string tempfile = file + '.' + uniq;

	if ((fd = open(tempfile.c_str(), O_CREAT|O_EXCL|O_WRONLY, 0600)) == -1)
		throw system_error(_("can't open tempfile"));

	int len = uniq.size();
	if (write(fd, uniq.c_str(), len) != len) {
		close(fd);
		unlink(tempfile.c_str());
		throw system_error(_("can't write tempfile"));
	}
	close(fd);

	if (link(tempfile.c_str(), lockfile.c_str()) == 0) {
		bool ret = CheckLinkCount(tempfile);
		unlink(tempfile.c_str());
		if (!ret)
			throw system_error(_("can't link lockfile"));
		return;
	}

	if ((fd = open(lockfile.c_str(), O_RDWR)) == -1) {
		unlink(tempfile.c_str());
		throw system_error(_("can't open lockfile"));
	}
	char buf[32];
	len = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (len <= 0) {
		unlink(tempfile.c_str());
		throw system_error(_("can't read lockfile"));
	}
	buf[len] = '\0';
	pid_t pid;
	if ((pid = std::strtol(buf, (char **) 0, 10)) == 0) {
		unlink(tempfile.c_str());
		throw system_error(_("can't get process id from lockfile"));
	}
	if (kill(pid, 0) == 0)  {
		unlink(tempfile.c_str());
		throw system_error(_("process exists for lockfile"));
	}
	if (unlink(lockfile.c_str()) != 0) {
		unlink(tempfile.c_str());
		throw system_error(_("can't delete old lockfile"));
	}

	bool done = false;
	if (link(tempfile.c_str(), lockfile.c_str()) == 0 && CheckLinkCount(tempfile))
		done = true;

	unlink(tempfile.c_str());

	if (!done)
		throw system_error(_("can't link lock file after delete"));
}
