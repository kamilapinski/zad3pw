<div class="no-overflow"><h1>Asynchroniczny Egzekutor w C</h1>

<h5>Motywacja</h5>

<p>Współczesne systemy komputerowe często muszą radzić sobie z wieloma zadaniami jednocześnie: obsługą licznych połączeń sieciowych, przetwarzaniem dużych ilości danych czy komunikacją z urządzeniami zewnętrznymi. Tradycyjne podejście oparte na wielowątkowości może prowadzić do znacznego narzutu na zarządzanie wątkami, a także do problemów ze skalowalnością w systemach o ograniczonych zasobach.</p>

<p>Asynchroniczne egzekutory (ang. <em>asynchronous executors</em>) stanowią nowoczesne rozwiązanie tego problemu. Zamiast uruchamiać każde zadanie w oddzielnym wątku, egzekutor zarządza zadaniami w sposób <em>kooperacyjny</em> – zadania "ustępują" sobie nawzajem procesora w momentach, gdy czekają na zasoby, np. dane z pliku, sygnały z urządzenia czy odpowiedź z bazy danych. Dzięki temu można obsłużyć tysiące lub nawet miliony jednoczesnych operacji przy użyciu ograniczonej liczby wątków.</p>

<h5>Przykłady Zastosowań</h5>

<ol>
<li><p><em>Serwery HTTP</em>.
Serwery takie jak Nginx czy Tokio w Rust wykorzystują asynchroniczne egzekutory do obsługi tysięcy połączeń sieciowych, jednocześnie minimalizując zużycie pamięci i procesora.</p></li>
<li><p><em>Programowanie systemów wbudowanych</em>.
W systemach o ograniczonych zasobach (np. mikrokontrolerach) efektywne zarządzanie wieloma zadaniami, takimi jak odczytywanie danych z czujników czy sterowanie urządzeniami, jest kluczowe. Asynchroniczność pozwala unikać kosztownej wielowątkowości.</p></li>
<li><p><em>Aplikacje interaktywne</em>.
W aplikacjach graficznych lub mobilnych asynchroniczność zapewnia płynność działania – np. podczas pobierania danych z sieci interfejs użytkownika pozostaje responsywny.</p></li>
<li><p><em>Przetwarzanie danych</em>.
Asynchroniczne podejście pozwala efektywnie zarządzać wieloma równoczesnymi operacjami wejścia-wyjścia, np. w systemach analizy danych, gdzie operacje dyskowe mogą stanowić wąskie gardło.</p></li>
</ol>

<h4>Cel zadania</h4>

<p>W ramach tego zadania domowego napiszecie uproszczoną wersję jednowątkowego asynchronicznego egzekutora w języku C (inspirowaną biblioteką Tokio w języku Rust).
W tym zadaniu domowym <strong>nie tworzymy nigdzie wątków</strong>: egzekutor i wykonywane przez niego zadania wszystkie działają w wątku głównym (lub ustępują innym czekając na np. możliwość czytania).</p>

<p>Otrzymacie obszerny szkielet projektu i będziecie mieli za zadanie zaimplementować kluczowe elementy. W efekcie powstanie prosta, ale funkcjonalna biblioteka asynchroniczna.
Przykłady jej zastosowania wraz z opisami znajdują się&nbsp;w plikach źródłowych w <code>tests/</code> (które stanowią też podstawowe, przykładowe testy).</p>

<p>Kluczową rolę w zadaniu spełnia mechanizm <code>epoll</code>, który pozwala czekać na wybrany zbiór zdarzeń. Chodzi przede wszystkim o nasłuchiwanie na dostępność deskryptorów (np. łącz lub socketów sieciowych) do czytania i pisania. Patrz:</p>

<ul>
<li><a href="https://suchprogramming.com/epoll-in-3-easy-steps/">epoll in 3 easy steps</a></li>
<li><a href="https://man7.org/linux/man-pages/man7/epoll.7.html">man epoll</a>, konkretnie będziemy używali jedynie:

<ul>
<li><code>epoll_create1(0)</code> (czyli <code>epoll_create</code> bez sugerowanego rozmiaru, bez specjalnych flag);</li>
<li><code>epoll_ctl(...)</code> tylko na deskryptorach łączy (<em>pipe</em>), tylko na zdarzeniach typu EPOLLIN/EPOLLOUT (gotowość&nbsp;na read/write), bez specjalnych flag (w szczególności w domyślnym trybie <em>level-triggered</em>, czyli bez flagi <code>EPOLLET</code>, nie <em>edge-triggered</em>);</li>
<li><code>epoll_wait(...)</code>;</li>
<li><code>close()</code> żeby zamknąć utworzony <code>epoll_fd</code>.</li>
</ul></li>
</ul>

<h2>Specyfikacja</h2>

<h4>Struktury</h4>

<p>Najważniejsze struktury potrzebne w implementacji to:</p>

<ul>
<li><code>Executor</code>: odpowiedzialny za wykonywanie zadań na procesorze – implementuje główną pętlę zdarzeń <code>executor_run()</code> i zawiera kolejkę&nbsp;zadań (w ogólności mógłby zawierać np. wiele kolejek i wątki do równoległego ich przetwarzania);</li>
<li><code>Mio</code>: odpowiedzialny za komunikację z systemem operacyjnym – implementuje czekanie na np. dostępność danych do odczytu, tzn. woła funkcje <code>epoll_*</code>.</li>
<li><p><code>Future</code>: w tej pracy domowej to nie tylko miejsce na wartość, na którą można zaczekać. <code>Future</code> będzie też zawierać korutynę (<em>coroutine</em>), czyli informacje o tym:</p>

<ul>
<li>jakie zadanie ma być wykonane (w postaci wskaźnika na funkcję <code>progress</code> ),</li>
<li>stan który trzeba zachować między kolejnymi etapami wykonania zadania (jeśli jakieś są).</li>
</ul>

<p>W C nie ma dziedziczenia, więc zamiast podklas klasy Future będziemy mieli struktury <code>FutureFoo</code>
zawierające <code>Future base</code> jako pierwsze pole i będziemy rzutować <code>Future*</code> na <code>FutureFoo*</code>.</p></li>
<li><p><code>Waker</code>: to callback (tu: wskaźnik na funkcję wraz z argumentami) definiujący jak powiadomić egzekutora o tym, że zadanie może postępować dalej (np. zadanie czekało na dane do odczytania i stały się&nbsp;one dostępne).</p></li>
</ul>

<p>W zadaniu domowym dany jest szkielet implementacji (pliki nagłówkowe i część struktur), oraz przykładowe future'y.
Pozostaje Wam implementacja egzekutora, Mio, oraz innych future'ów (szczegóły niżej).</p>

<h4>Schemat działania</h4>

<p>Zadanie (<code>Future</code>) może być w jednym z czterech stanów:</p>

<ul>
<li><code>COMPLETED</code>: zadanie się zakończyło i ma wynik.</li>
<li><code>FAILURE</code>: zadanie się&nbsp;zakończyło błędem lub zostało przerwane.</li>
<li><code>PENDING(zakolejkowany)</code>: może postępować, czyli jest właśnie wykonywane przez egzekutora lub czeka na przydział procesora w egzekutorze w kolejce.</li>
<li><code>PENDING(waker czeka)</code>: zadanie nie mogło dalej postępować w egzekutorze i ktoś&nbsp;(np. <code>Mio</code> lub wątek pomocniczy) trzyma <code>Waker</code>a, którego wywoła gdy to się&nbsp;zmieni.</li>
</ul>

<p>Diagram stanów wygląda następująco:</p>

<pre><code>executor_spawn                     COMPLETED / FAILURE
     │                                       ▲
     ▼               executor woła           │
   PENDING  ───► fut-&gt;progress(fut, waker) ──+
(zakolejkowany)                              │
     ▲                                       │
     │                                       ▼
     └─── ktoś (np. mio_poll) woła ◄──── PENDING
              waker_wake(waker)       (waker czeka)
</code></pre>

<p>(W kodzie będziemy traktować PENDING jako jeden stan, bez rozróżniania kto trzyma wskaźnik na <code>Future</code>.)</p>

<p>Bardziej szczegółowo, schemat działania wygląda tak:</p>

<ul>
<li>program tworzy egzekutora (który tworzy dla siebie instancję Mio).</li>
<li>wrzuca do egzekutora zadania (<code>executor_spawn</code>).</li>
<li>woła <code>executor_run</code>, które będzie wykonywać zadania, które będą&nbsp;zlecać podzadania, tak aż do zakończenia programu.</li>
<li>W tym zadaniu domowym egzekutor nie tworzy wątków: <code>executor_run()</code> pracuje w tym wątku, w którym został zawołany.</li>
</ul>

<p>Egzekutor w pętli przetwarza zadania:</p>

<ul>
<li>jeśli nie ma już niezakończonych (<code>PENDING</code>) zadań, kończy pętlę.</li>
<li>jeśli nie ma aktywnych zadań, woła <code>mio_poll()</code> żeby uśpić wątek egzekutora aż się&nbsp;to zmieni.</li>
<li>dla każdego aktywnego zadania <code>future</code>, woła <code>future.progress(future, waker)</code> (tworząc przy tym <code>Waker</code> który doda zadanie z powrotem do kolejki, jeśli będzie potrzeba).</li>
</ul>

<p>Future w funkcji <code>progress</code>:</p>

<ul>
<li>próbuje zrobić postęp w zadaniu (np. odczytać więcej bajtów z łącza, czy wykonać etap obliczeń).</li>
<li>w razie błędu, zwraca stan <code>FAILURE</code>.</li>
<li>jeśli uda się&nbsp;zakończyć zadanie, zwraca stan <code>COMPLETED</code>.</li>
<li>w przeciwnym wypadku zwraca stan <code>PENDING</code>, upewniwszy się że ktoś (np. <code>Mio</code>, wątek pomocniczy, lub sam) kiedyś&nbsp;zawoła <code>waker</code>a.</li>
</ul>

<p>Mio pozwala:</p>

<ul>
<li>w funkcji <code>mio_register</code>: zarejestrować, że dany <code>waker</code> ma zostać wywołany, gdy zajdzie odpowiednie zdarzenie; te zdarzenia to gotowość danego deskryptora do odczytu lub zapisu (w przypadku tego zadania domowego). Mio ma za zadanie zapewnić, że zawoła <code>waker</code>a dopiero gdy na danym zasobie (deskryptorze) można wykonać operację <code>read/write</code> w sposób nieblokujący.</li>
<li>w funkcji <code>mio_poll</code>: uśpić wołający wątek aż dojdzie do co najmniej jednego z zarejestrowanych zdarzeń i wywołać dla nich odpowiedniego <code>waker</code>a.</li>
</ul>

<h4>Co może robić <code>future.progress()</code></h4>

<p>Nisko-poziomowe zadania w ramach swojego <code>progress()</code> mogą wołać np.:</p>

<ul>
<li><code>mio_register(..., waker)</code>, żeby móc zwrócić <code>PENDING</code> i być później zawołanym przez egzekutora ponownie (dopiero gdy coś stanie się gotowe, przejść&nbsp;wtedy do następnego etapu obliczeń);</li>
<li><code>waker_wake(waker)</code>, żeby samemu bezpośrednio poprosić egzekutora o natychmiastowe zakolejkowanie – to ma sens jeśli chcemy zwrócić <code>PENDING</code> i pozwolić tym samym innym zadaniom się&nbsp;wykonać przed przejściem do następnego etapu dłuższych obliczeń (analogicznie do <code>Thread.yield()</code>);</li>
<li><code>other_future-&gt;progress(other_future, waker)</code>, żeby spróbować wykonać podzadanie, w ramach własnego przydziału procesora;</li>
<li><code>executor_spawn</code>, żeby zlecić egzekutorowi&nbsp;podzadanie, które może wykonywać się&nbsp;niezależnie.</li>
<li>(nie w tym zadaniu domowym) <code>pthread_create</code>, żeby wykonać obliczenia w tle, równolegle z pracą egzekutora;</li>
</ul>

<p>Przykłady implementacji <code>Future</code> znajdziecie w <code>future_examples.h/c</code> oraz <code>tests/hard_work_test.c</code>. Przykłady użycia i działania całości w <code>tests/</code>.</p>

<h4>Uwagi o <code>Waker</code></h4>

<p>Waker w tym zadaniu domowym:</p>

<ul>
<li>Zawsze woła tę samą funkcję (zakolejkowanie u egzekutora) bo używamy tylko jednej implementacji egzekutora.</li>
<li>Dlatego <code>Waker</code> nie zawiera wskaźnika na funkcję, a jedynie argumenty do <code>waker_wake()</code>: <code>Executor*</code> i <code>Future*</code>.</li>
<li>Uwaga: w wywołaniu <code>future1.progress(waker)</code>, <code>waker.future</code> nie musi być być tym samym co <code>future1</code> –&nbsp;może być nad-zadaniem, które wywołuje <code>future1</code> jako podzadanie.</li>
<li>Natomiast w <code>Mio</code> można założyć, że <code>waker.executor</code> jest tym samym co <code>executor</code> podany przy <code>mio_create()</code> (dzięki temu w <code>mio_register</code> wystarczy zapamiętać <code>Future*</code>, bez alokacji miejsca na cały <code>Waker</code>).</li>
</ul>

<h4><code>Future</code> do zaimplementowania: kombinatory</h4>

<p>Ostatnią częścią&nbsp;tego zadania domowego jest zaimplementowanie trzech <em>kombinatorów</em>, czyli Future, które łączą inne Future:</p>

<ul>
<li>Wykonanie <code>ThenFuture</code> stworzonego z <code>fut1</code> i <code>fut2</code> ma w efekcie wykonać <code>fut1</code>, a następnie <code>fut2</code>, przekazując wynik <code>fut1</code> jako argument wejściowy dla <code>fut2</code>.</li>
<li><code>JoinFuture</code> ma wykonać <code>fut1</code> i <code>fut2</code> współbieżnie, kończąc gdy oba zakończą działanie.</li>
<li><code>SelectFuture</code> ma wykonać <code>fut1</code> i <code>fut2</code> współbieżnie, kończąc gdy którykolwiek pomyślnie zakończy działanie;
gdy np. <code>fut1</code>  pomyślnie skończy działanie (stanem <code>COMPLETED</code>), drugi ma zostać porzucony,
czyli <code>fut2.progress()</code> nie będzie dalej wołane, nawet jeśli ostatni stan zwrócony przez <code>fut2.progress()</code> był <code>PENDING</code>.</li>
</ul>

<p>Szczegółowy interfejs i zachowanie (w tym obsługa błędów) wyspecifikowane są w <code>future_combinators.h</code>.</p>

<h2>Wymagania formalne</h2>

<h4>Dane</h4>

<p><strong>UWAGA: modyfikowanie danych plików jest zabronione, za wyjątkiem tych, które są wymienione jako do uzupełnienia przez studenta.</strong></p>

<p>Dane są pliki nagłówkowe:</p>

<ul>
<li><code>executor.h</code> - egzekutor asynchroniczny,</li>
<li><code>future.h</code> - obliczenie/zadanie asynchroniczne (<em>korutyna</em>),</li>
<li><code>waker.h</code> - mechanizm wybudzania zadania asynchronicznego po zajściu oczekiwanego zdarzenia,</li>
<li><code>mio.h</code> - warstwa abstrakcji ponad wywołaniem systemowym <code>epoll</code>, służąca do rejestrowania w systemie operacyjnym zainteresowania dostępnością operacji wejścia/wyjścia,</li>
<li><code>future_combinators.h</code> - operatory wiążące logicznie ze sobą wykonanie dwóch obliczeń asynchronicznych, np. relacją następstwa (<em>Then</em>) lub wykonania współbieżnego (<em>Join</em>),</li>
</ul>

<p>Dane są pliki źródłowe <strong>do uzupełnienia przez studenta</strong>: <code>executor.c</code>, <code>mio.c</code>, <code>future_combinators.c</code>.</p>

<p>Dodatkowo, załączone są nagłówki:</p>

<ul>
<li><code>err.h</code> - znana z laboratoriów mała biblioteka do wygodnej obsługi błędów.</li>
<li><code>debug.h</code> - proste makro <code>debug()</code> do pisania na standardowe wyjście, włączane flagą <code>DEBUG_PRINTS</code>.</li>
</ul>

<p>Pozostałe pliki: <code>future_examples.{h,c}</code> oraz zawartość podkatalogu <code>tests</code> służą do testowania biblioteki oraz ilustrowania jej funkcjonalności.</p>

<h4>Ocenianie</h4>

<p>Do wykonania są trzy komponenty, w kolejności: <code>executor.c</code>, <code>mio.c</code>, oraz (największy) <code>future_combinators.c</code>.</p>

<p>Komponenty będą testowane jednostkowo (indywidualnie, niezależnie od pozostałych) oraz integracyjnie (jako całość).
Punkty będą przyznawane za każdy komponent indywidualnie oraz za działanie całości.</p>

<h4>Gwarancje</h4>

<ul>
<li>W tym zadaniu domowym nie będziemy nigdzie tworzyli wątków; <em>wszystko</em> dzieje się&nbsp;w jednym, głównym wątku (w szczególności <code>executor_run</code>, <code>future.progress</code>, <code>waker_wake</code>).</li>
<li>Wykonanie <code>executor_run</code> i  <code>executor_destroy</code> dla każdego <code>executor_create</code> nastąpi dokładnie raz.</li>
<li>Wykonanie <code>executor_destroy</code> nastąpi zawsze po wywołaniu i zakończeniu odpowiadającego <code>executor_run</code>.</li>
<li>Wywołania <code>executor_spawn</code> mogą mieć miejsce zarówno spoza kontekstu <code>executor_run</code>, jak i w trakcie działania <code>executor_run</code>. Należy poprawnie obsługiwać obydwa przypadki.</li>
<li>Można założyć, że liczba <code>Future</code>ów w stanie <code>PENDING</code> nie przekroczy <code>max_queue_size</code> podanej do <code>executor_create</code>.</li>
</ul>

<h4>Wymagania</h4>

<ul>
<li>Rozwiązaniem powinno być archiwum postaci takiej jak załączony szablon <code>ab12345.zip</code>, czyli plik ZIP o nazwie <code>ab12345.zip</code> zawierający wyłącznie folder o nazwie <code>ab12345</code>, gdzie zamiast <code>ab12345</code> należy użyć własnych inicjałów i numeru indeksu (czyli nazwy użytkownika na students).</li>
<li>Z rozwiązania zostaną&nbsp;pobrane jedynie trzy pliki do uzupełnienia przez studenta; nowe i zmienione pliki (w szczególności zmiany w CMakeLists.txt) zostaną zignorowane.</li>
<li>Nie można stosować aktywnego ani półaktywnego oczekiwania.

<ul>
<li>Tym samym w uzupełnianych plikach rozwiązania nie należy korzystać z żadnej funkcji usypiającej
na określony czas (<code>sleep</code>, <code>usleep</code>, <code>nanosleep</code>) ani podawać timeout'ów (do np. <code>epoll_wait</code>).</li>
</ul></li>
<li>Rozwiązania będą testowane pod kątem wycieków pamięci i/lub innych zasobów (niezamkniętych plików itd.).</li>
<li>Implementacje powinny być rozsądnie efektywne, tzn. niewystawione na ekstremalne obciążenie (np. obsługę setek tysięcy obliczeń) nie powinny dodawać poza oczekiwany czas działania ( wynikający z kosztu obliczenia lub oczekiwania na deskryptorze) narzutu rzędu dziesiątek milisekund (ani większego).</li>
<li>Wymagamy użycia języka C w wersji <code>gnu11</code> (ew. <code>c11</code>).</li>
<li>Można korzystać ze standardowej biblioteki języka C (<code>libc</code>) i funkcjonalności dostarczanych przez system (zadeklarowanych w <code>unistd.h</code> itp.).</li>
<li>Niedozwolone jest korzystanie z innych bibliotek zewnętrznych.</li>
<li>Można zapożyczać dowolny kod z laboratoriów. Wszelkie inne ewentualne zapożyczenia kodu należy odpowiednio komentować z podaniem źródła.</li>
</ul>

<h4>Pomocne komendy</h4>

<ul>
<li>Przygotowanie budowania: <code>mkdir build; cd build; cmake ..</code></li>
<li>Budowanie testów: <code>cd build/tests; cmake --build ..</code></li>
<li>Uruchamianie testów zbiorczo: <code>cd build/tests; ctest --output-on-failure</code></li>
<li>Pojedynczo: <code>./&lt;nazwa_testu&gt;</code></li>
<li>Valgrind: w szczególności flagi <code>--track-origins=yes --track-fds=yes</code></li>
<li><em>ASAN</em> (Address Sanitizer): uruchamiamy nasz program po skompilowaniu z flagą <code>-fsanitize=address</code> do <code>gcc</code> (domyślnie już ustawiona w załączonym CMakeLists.txt).</li>
</ul>
</div>
