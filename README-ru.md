# libnss-role

Библиотека для NSS и набор инструментов для администрирования ролей и привилегий.
Этот README также доступен на [английском (English)](README.md)

## Описание

Модуль ролей - это модуль для службы переключения имён [NSS](https://en.wikipedia.org/wiki/Name_Service_Switch).
Модуль реализует возможность добавления групп в группы. Для администрирования модуля ролей реализованы специальные вспомогательные утилиты, которые рассматривают все группы поделёнными на две категории: роли и привилегии.

`libnss-role` позволяет достичь следующего:

* пусть пользователь `vasya` состоит в группе `users`;
* мы хотим, чтобы все пользователи, состоящие в группе `users`, автоматически считались членами группы `cdrom`, но не хотим каждого члена группы `users` вручную добавлять в `cdrom`;
* `libnss-role` делает так, чтобы, когда у `glibc` запрашивают, в каких группах состоит пользователь, "изменять" ответ в соответствии с описанным выше правилом.

В дистрибутивах GNU/Linux почти все программы и библиотеки слинкованы с системной библиотекой `libc.so.6` (glibc), а значит, что, если они используют стандартные методы получения информации о пользователе, `libnss-role` отработает. Если исполняемый файл статично слинкован с любой libc или иными методами использует не системную libc, `libnss-role` не отработает для запросов от этой программы.

### Привилегия
Обычная POSIX-группа, которая может быть назначена пользователю.
После этого пользователь может совершать действия, соответствующие данной привилегии.
К привилегиям относятся такие группы, как `cdwriter`, `audio`, `serial`, `virtualbox` и др.

### Роль
Роль так же является POSIX группой, но её предназначение указывать, на характерную деятельность, которую выполняет пользователь, работая за компьютером.
Такими группами могут быть группы `admin`, `user`, `power_user`, `developer` и др.

Для того, чтобы пользователь мог выполнять определённую роль ему могут понадобиться определённые права (привилегии), поэтому полезно уметь назначать привилегии как непосредственно пользователю (как это делается в POSIX), так и косвенно добавлять ему наборы привилегий через назначение роли. Например пользователь, которому назначена роль admin мог бы получать кроме группы admin, ещё и группы-привилегии wheel, ssh, root и пр.

Модуль ролей позволяет реализовать подобное назначение прав пользователям системы. Группы-роли назначаются непосредственно пользователю, а группы-привилегии назначаются как пользователю непосредственно, так и группам-ролям. Деление групп на два сорта условно, и администратор может управлять им самостоятельно.

## Сборка
Чтобы скомпилировать проект, вам понадобятся:
* scons
* компилятор C++ (например, gcc-c++)
* заголовочные файлы от glibc, pam

После установки всех зависимостей войдите в директорию с исходными кодами и выполните команду:

```
$ scons
```

Для установки выполните от имени суперпользователя:
```
# scons install
```

Возможно вам придется создать конфигурационный файл с информацией о ролях:
```
# touch /etc/role
```
Теперь необходимо включить модуль ролей.
Для этого откройте редактором файл с настройками NSS `/etc/nsswitch.conf` и в строку, начинающуюся с `groups:`
в самом конце добавьте: `role`. Должна получится строка наподобие такой:
```
groups: files ldap role
```

## Администрирование модуля
### Файл конфигурации
Для хранения и получения информации о ролях модуль использует файл `/etc/role`, а также стандартный файл `/etc/group`.
`/etc/group` - стандартный файл в POSIX и описан во многих руководствах. `/etc/role` хранит дополнительную информацию о вхождении групп в группы.
Каждая строка в файле `/etc/role` имеет следующий формат:
```
<group_id>:<group_id>[,<group_id>]*
```

где `<group_id>` - целочисленный идентификатор группы.
Примечание: мы долго думали, что использовать в файле настроек - имена или идентификаторы. Остановились на идентификаторах, поскольку утилиты в Linux умеют переименовывать группы. При этом стандартные файлы перерабатываются корректным образом, а вот наш начнёт ссылаться на случайные группы, что может привести к проблемам в безопасности. Смена же идентификаторов недопустима, так как в этом случае будут затронуты права на файловой системе.

Идентификатор до ":" означает, что данная группа будет являться ролью или, что то же самое, будет входить в другие группы, а последующие идентификаторы означают в какие группы входит данная или какие привилегии и роли ей назначены. Вхождения групп в группы применяется рекурсивно.

Для наглядности приведем пример. Пусть у нас есть пользователь `pupkin`. Пусть в файле `/etc/group` имеются записи:
```
group1:x:1:pupkin
group2:x:2:pupkin
group3:x:3:
group4:x:4:
group5:x:5:
group6:x:6:
```

А файл /etc/role содержит:
```
2:3,4
4:5,6
```

Тогда пользователь `pupkin` получит все имеющиеся группы. Группы group1 и group2, так как они назначены ему непосредственно, group3 и group4 - потому что они назначены группе group2, а группы group5 и group6 - так как они назначены группе group4.
Так как использование идентификаторов делает ручную правку файла делом трудоёмким, то созданы утилиты для администрирования модуля.

### Административные утилиты
Существуют три утилиты для выполнения задач по администрированию модуля: `roleadd`, `roledel` и `rolelst`.

#### roleadd
Вызов:
```
roleadd [-s] ROLE [GROUP*]
```

Добавляет роль (если её ещё нет) и назначает ей привилегии и группы.
ROLE - имя роли. Должно совпадать с уже имеющейся группой.
GROUP - имя роли или привилегии, которые будут назначены данной (если используется вместе с параметром `-s`, то производится установка групп; по умолчанию группы добавляются к уже имеющимся)

#### roledel
Вызов:
```
roledel ROLE [GROUP*]
roledel -r ROLE
```

Используется для удаления назначенных роли привилегий и ролей (первая форма), а так же для удаления самих ролей (вторая форма)

#### rolelst
Вызов:
```
rolelst
```
Показывает информацию из `/etc/role`, преобразуя идентификаторы к именам.
